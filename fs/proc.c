#include <string.h>
#include <sys/stat.h>
#include "kernel/calls.h"
#include "kernel/fs.h"

struct proc_entry {
    const char *name;
    mode_t_ mode;
    // different kinds of proc entries
    struct {
        // file with custom show data function
        // not worrying about buffer overflows for now
        size_t (*show)(struct proc_entry *entry, char *buf);
        // directory with static list
        struct proc_entry *children;
        size_t children_sizeof;
    };
};

static size_t proc_show_version(struct proc_entry *entry, char *buf) {
    struct uname uts;
    do_uname(&uts);
    return sprintf(buf, "%s version %s %s\n", uts.system, uts.release, uts.version);
}

static struct proc_entry proc_root_entries[] = {
    {"version", S_IFREG | 0444, {.show = proc_show_version}},
};

static struct proc_entry proc_root = {NULL, S_IFDIR | 0555,
    {.children = proc_root_entries, .children_sizeof = sizeof(proc_root_entries)}};

static bool proc_dir_read(struct proc_entry *entry, int *index, struct proc_entry **next_entry) {
    if (entry->children) {
        if (*index >= entry->children_sizeof/sizeof(struct proc_entry))
            return false;
        *next_entry = &entry->children[*index];
        (*index)++;
        return true;
    }
    assert(!"read from invalid proc directory");
}

static int proc_entry_stat(struct proc_entry *entry, struct statbuf *stat) {
    memset(stat, 0, sizeof(*stat));
    stat->mode = entry->mode;
    return 0;
}

extern const struct fd_ops procfs_fdops;

static struct proc_entry *proc_lookup(const char *path) {
    struct proc_entry *entry = &proc_root;
    char component[MAX_NAME] = {};
    while (*path != '\0') {
        if (!S_ISDIR(entry->mode))
            return ERR_PTR(_ENOTDIR);

        assert(*path == '/');
        path++;
        char *c = component;
        while (*path != '/' && *path != '\0') {
            *c++ = *path++;
            if (c - component >= sizeof(component))
                return ERR_PTR(_ENAMETOOLONG);
        }

        int index = 0;
        struct proc_entry *next_entry;
        while (proc_dir_read(entry, &index, &next_entry)) {
            if (strcmp(next_entry->name, component) == 0)
                goto found;
        }
        return ERR_PTR(_ENOENT);
found:
        entry = next_entry;
    }
    return entry;
}

static struct fd *proc_open(struct mount *mount, const char *path, int flags, int mode) {
    struct proc_entry *entry = proc_lookup(path);
    if (IS_ERR(entry))
        return ERR_PTR(PTR_ERR(entry));
    struct fd *fd = fd_create();
    fd->ops = &procfs_fdops;
    fd->proc_entry = entry;
    fd->proc_data = NULL;
    return fd;
}

static int proc_stat(struct mount *mount, const char *path, struct statbuf *stat, bool follow_links) {
    struct proc_entry *entry = proc_lookup(path);
    if (IS_ERR(entry))
        return PTR_ERR(entry);
    return proc_entry_stat(entry, stat);
}

static int proc_fstat(struct fd *fd, struct statbuf *stat) {
    return proc_entry_stat(fd->proc_entry, stat);
}

static void proc_refresh_data(struct fd *fd) {
    if (fd->proc_data == NULL) {
        fd->proc_data = malloc(4096); // FIXME choose a good number
    }
    struct proc_entry *entry = fd->proc_entry;
    fd->proc_size = entry->show(entry, fd->proc_data);
}

static ssize_t proc_read(struct fd *fd, void *buf, size_t bufsize) {
    struct proc_entry *entry = fd->proc_entry;
    if (!S_ISREG(entry->mode))
        return _EISDIR;
    proc_refresh_data(fd);

    const char *data = fd->proc_data;
    assert(data != NULL);

    size_t remaining = fd->proc_size - fd->offset;
    if (fd->offset > fd->proc_size)
        remaining = 0;
    size_t n = bufsize;
    if (n > remaining)
        n = remaining;

    memcpy(buf, data, n);
    fd->offset += n;
    return n;
}

static off_t_ proc_seek(struct fd *fd, off_t_ off, int whence) {
    if (!S_ISREG(fd->proc_entry->mode))
        return _EISDIR;
    proc_refresh_data(fd);

    off_t_ old_off = fd->offset;
    if (whence == LSEEK_SET)
        fd->offset = off;
    else if (whence == LSEEK_CUR)
        fd->offset += off;
    else if (whence == LSEEK_END)
        fd->offset = fd->proc_size + off;
    else
        return _EINVAL;

    if (fd->offset < 0) {
        fd->offset = old_off;
        return _EINVAL;
    }
    return fd->offset;
}

static int proc_readdir(struct fd *fd, struct dir_entry *entry) {
    struct proc_entry *proc_entry;
    bool any_left = proc_dir_read(fd->proc_entry, &fd->proc_dir_index, &proc_entry);
    if (!any_left)
        return 0;
    strcpy(entry->name, proc_entry->name);
    entry->inode = 0;
    return 1;
}

static long proc_telldir(struct fd *fd) {
    return fd->proc_dir_index;
}
static int proc_seekdir(struct fd *fd, long ptr) {
    fd->proc_dir_index = ptr;
    return 0;
}

const struct fd_ops procfs_fdops = {
    .read = proc_read,
    .lseek = proc_seek,

    .readdir = proc_readdir,
    .telldir = proc_telldir,
    .seekdir = proc_seekdir,
};

static ssize_t proc_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize) {
    return _EINVAL;
}

const struct fs_ops procfs = {
    .name = "proc",
    .open = proc_open,
    .stat = proc_stat,
    .fstat = proc_fstat,
    .readlink = proc_readlink,
};
