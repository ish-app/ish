#include <string.h>
#include <sys/stat.h>
#include "kernel/calls.h"
#include "kernel/fs.h"
#include "fs/proc.h"

static int proc_lookup(const char *path, struct proc_entry *entry) {
    entry->meta = &proc_root;
    char component[MAX_NAME] = {};
    while (*path != '\0') {
        if (!S_ISDIR(entry->meta->mode))
            return _ENOTDIR;

        assert(*path == '/');
        path++;
        char *c = component;
        while (*path != '/' && *path != '\0') {
            *c++ = *path++;
            if (c - component >= sizeof(component))
                return _ENAMETOOLONG;
        }

        int index = 0;
        struct proc_entry next_entry;
        char entry_name[MAX_NAME];
        while (proc_dir_read(entry, &index, &next_entry)) {
            proc_entry_getname(&next_entry, entry_name);
            if (strcmp(entry_name, component) == 0)
                goto found;
        }
        return _ENOENT;
found:
        *entry = next_entry;
    }
    return 0;
}

extern const struct fd_ops procfs_fdops;

static struct fd *proc_open(struct mount *mount, const char *path, int flags, int mode) {
    struct proc_entry entry;
    int err = proc_lookup(path, &entry);
    if (err < 0)
        return ERR_PTR(err);
    struct fd *fd = fd_create();
    fd->ops = &procfs_fdops;
    fd->proc_entry = entry;
    fd->proc_data = NULL;
    return fd;
}

static int proc_stat(struct mount *mount, const char *path, struct statbuf *stat, bool follow_links) {
    struct proc_entry entry;
    int err = proc_lookup(path, &entry);
    if (err < 0)
        return err;
    return proc_entry_stat(&entry, stat);
}

static int proc_fstat(struct fd *fd, struct statbuf *stat) {
    return proc_entry_stat(&fd->proc_entry, stat);
}

static int proc_refresh_data(struct fd *fd) {
    if (fd->proc_data == NULL) {
        fd->proc_data = malloc(4096); // FIXME choose a good number
    }
    struct proc_entry entry = fd->proc_entry;
    ssize_t size = entry.meta->show(&entry, fd->proc_data);
    if (size < 0)
        return size;
    fd->proc_size = size;
    return 0;
}

static ssize_t proc_read(struct fd *fd, void *buf, size_t bufsize) {
    if (!S_ISREG(fd->proc_entry.meta->mode))
        return _EISDIR;
    int err = proc_refresh_data(fd);
    if (err < 0)
        return err;

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
    if (!S_ISREG(fd->proc_entry.meta->mode))
        return _EISDIR;
    int err = proc_refresh_data(fd);
    if (err < 0)
        return err;

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
    struct proc_entry proc_entry;
    bool any_left = proc_dir_read(&fd->proc_entry, &fd->proc_dir_index, &proc_entry);
    if (!any_left)
        return 0;
    proc_entry_getname(&proc_entry, entry->name);
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
    .name = "proc", .magic = 0x9fa0,
    .open = proc_open,
    .stat = proc_stat,
    .fstat = proc_fstat,
    .readlink = proc_readlink,
};
