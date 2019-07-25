#include <string.h>
#include <sys/stat.h>
#include "kernel/calls.h"
#include "kernel/fs.h"
#include "fs/proc.h"

static int proc_lookup(const char *path, struct proc_entry *entry) {
    entry->meta = &proc_root;
    char component[MAX_NAME + 1] = {};
    while (*path != '\0') {
        if (!S_ISDIR(proc_entry_mode(entry)))
            return _ENOTDIR;

        assert(*path == '/');
        path++;
        char *c = component;
        while (*path != '/' && *path != '\0') {
            *c++ = *path++;
            if (c - component >= MAX_NAME)
                return _ENAMETOOLONG;
        }
        *c = '\0';

        unsigned long index = 0;
        struct proc_entry next_entry;
        char entry_name[MAX_NAME];
        while (proc_dir_read(entry, &index, &next_entry)) {
            // tack on some dynamically generated attributes
            if (next_entry.meta->parent == NULL)
                next_entry.meta->parent = entry->meta;
            else
                // this asserts that an entry has a unique parent
                assert(next_entry.meta->parent == entry->meta);
            if (next_entry.meta->inode == 0)
                next_entry.meta->inode = index + 1;

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

static struct fd *proc_open(struct mount *UNUSED(mount), const char *path, int UNUSED(flags), int UNUSED(mode)) {
    struct proc_entry entry;
    int err = proc_lookup(path, &entry);
    if (err < 0)
        return ERR_PTR(err);
    struct fd *fd = fd_create(&procfs_fdops);
    fd->proc.entry = entry;
    fd->proc.data = NULL;
    return fd;
}

static int proc_getpath(struct fd *fd, char *buf) {
    char *p = buf + MAX_PATH - 1;
    size_t n = 0;
    p[0] = '\0';
    struct proc_entry entry = fd->proc.entry;
    while (entry.meta != &proc_root) {
        char component[MAX_NAME];
        proc_entry_getname(&entry, component);
        size_t component_len = strlen(component) + 1; // plus one for the slash
        p -= component_len;
        n += component_len;
        *p = '/';
        memcpy(p + 1, component, component_len);
        entry.meta = entry.meta->parent;
    }
    memmove(buf, p, n + 1); // plus one for the null
    return 0;
}

static int proc_stat(struct mount *UNUSED(mount), const char *path, struct statbuf *stat, bool UNUSED(follow_links)) {
    struct proc_entry entry;
    int err = proc_lookup(path, &entry);
    if (err < 0)
        return err;
    return proc_entry_stat(&entry, stat);
}

static int proc_fstat(struct fd *fd, struct statbuf *stat) {
    return proc_entry_stat(&fd->proc.entry, stat);
}

static int proc_refresh_data(struct fd *fd) {
    mode_t_ mode = proc_entry_mode(&fd->proc.entry);
    if (S_ISDIR(mode))
        return _EISDIR;
    assert(S_ISREG(mode));

    if (fd->proc.data == NULL) {
        fd->proc.data = malloc(4096); // TODO choose a good number
    }
    struct proc_entry entry = fd->proc.entry;
    ssize_t size = entry.meta->show(&entry, fd->proc.data);
    if (size < 0)
        return (int)size;
    fd->proc.size = size;
    return 0;
}

static ssize_t proc_read(struct fd *fd, void *buf, size_t bufsize) {
    int err = proc_refresh_data(fd);
    if (err < 0)
        return err;

    const char *data = fd->proc.data;
    assert(data != NULL);

    size_t remaining = fd->proc.size - fd->offset;
    if ((size_t) fd->offset > fd->proc.size)
        remaining = 0;
    size_t n = bufsize;
    if (n > remaining)
        n = remaining;

    memcpy(buf, data + fd->offset, n);
    fd->offset += n;
    return n;
}

static off_t_ proc_seek(struct fd *fd, off_t_ off, int whence) {
    int err = proc_refresh_data(fd);
    if (err < 0)
        return err;

    off_t_ old_off = fd->offset;
    if (whence == LSEEK_SET)
        fd->offset = off;
    else if (whence == LSEEK_CUR)
        fd->offset += off;
    else if (whence == LSEEK_END)
        fd->offset = fd->proc.size + off;
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
    bool any_left = proc_dir_read(&fd->proc.entry, &fd->offset, &proc_entry);
    if (!any_left)
        return 0;
    proc_entry_getname(&proc_entry, entry->name);
    entry->inode = 0;
    return 1;
}

static int proc_close(struct fd *fd) {
    if (fd->proc.data != NULL)
        free(fd->proc.data);
    return 0;
}

const struct fd_ops procfs_fdops = {
    .read = proc_read,
    .lseek = proc_seek,
    .readdir = proc_readdir,
    .close = proc_close,
};

static ssize_t proc_readlink(struct mount *UNUSED(mount), const char *path, char *buf, size_t bufsize) {
    struct proc_entry entry;
    int err = proc_lookup(path, &entry);
    if (err < 0)
        return err;
    if (!S_ISLNK(proc_entry_mode(&entry)))
        return _EINVAL;

    char target[MAX_PATH + 1];
    err = entry.meta->readlink(&entry, target);
    if (err < 0)
        return err;
    if (bufsize > strlen(target))
        bufsize = strlen(target);
    memcpy(buf, target, bufsize);
    return bufsize;
}

const struct fs_ops procfs = {
    .name = "proc", .magic = 0x9fa0,
    .open = proc_open,
    .getpath = proc_getpath,
    .stat = proc_stat,
    .fstat = proc_fstat,
    .readlink = proc_readlink,
};
