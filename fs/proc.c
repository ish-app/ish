#include <string.h>
#include <sys/stat.h>
#include "kernel/calls.h"
#include "kernel/fs.h"
#include "fs/proc.h"
#include "fs/path.h"

static int proc_lookup(const char *path, struct proc_entry *entry) {
    entry->meta = &proc_root;
    char component[MAX_NAME + 1];
    int err = 0;
    while (path_next_component(&path, component, &err)) {
        if (!S_ISDIR(proc_entry_mode(entry)))
            return _ENOTDIR;

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
    return err;
}

extern const struct fd_ops procfs_fdops;

static struct fd *proc_open(struct mount *UNUSED(mount), const char *path, int UNUSED(flags), int UNUSED(mode)) {
    struct proc_entry entry;
    int err = proc_lookup(path, &entry);
    if (err < 0)
        return ERR_PTR(err);
    struct fd *fd = fd_create(&procfs_fdops);
    fd->proc.entry = entry;
    fd->proc.data.data = NULL;
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

static int proc_stat(struct mount *UNUSED(mount), const char *path, struct statbuf *stat) {
    struct proc_entry entry = {};
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

    if (fd->proc.data.data == NULL) {
        fd->proc.data.capacity = 4096;
        fd->proc.data.data = malloc(fd->proc.data.capacity); // default size
    }
    fd->proc.data.size = 0;
    struct proc_entry entry = fd->proc.entry;
    int err = entry.meta->show(&entry, &fd->proc.data);
    if (err < 0)
        return err;
    return 0;
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
        fd->offset = fd->proc.data.size + off;
    else
        return _EINVAL;

    if (fd->offset < 0) {
        fd->offset = old_off;
        return _EINVAL;
    }
    return fd->offset;
}

static ssize_t proc_pread(struct fd *fd, void *buf, size_t bufsize, off_t off) {
    int err = proc_refresh_data(fd);
    if (err < 0)
        return err;

    const char *data = fd->proc.data.data;
    assert(data != NULL);

    size_t remaining = fd->proc.data.size - off;
    if ((size_t) off > fd->proc.data.size)
        remaining = 0;
    size_t n = bufsize;
    if (n > remaining)
        n = remaining;

    memcpy(buf, data + off, n);
    return n;
}

static int proc_readdir(struct fd *fd, struct dir_entry *entry) {
    struct proc_entry proc_entry;
    bool any_left = proc_dir_read(&fd->proc.entry, &fd->offset, &proc_entry);
    if (!any_left)
        return 0;
    proc_entry_getname(&proc_entry, entry->name);
    entry->inode = proc_entry.meta->inode;
    return 1;
}

static int proc_close(struct fd *fd) {
    if (fd->proc.data.data != NULL)
        free(fd->proc.data.data);
    return 0;
}

const struct fd_ops procfs_fdops = {
    .pread = proc_pread,
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

void proc_buf_write(struct proc_data *buf, const void *data, size_t size) {
    if (buf->size + size > buf->capacity) {
        size_t new_capacity = buf->capacity;
        if (new_capacity == 0)
            new_capacity = 1;
        while (buf->size + size > new_capacity)
            new_capacity *= 2;
        char *new_data = realloc(buf->data, new_capacity);
        if (new_data == NULL) {
            // just give up, error reporting is a pain
            return;
        }
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
    assert(buf->size + size <= buf->capacity);
    memcpy(&buf->data[buf->size], data, size);
    buf->size += size;
}

void proc_printf(struct proc_data *buf, const char *format, ...) {
    char data[4096];
    va_list args;
    va_start(args, format);
    size_t size = vsnprintf(data, sizeof(data), format, args);
    va_end(args);
    proc_buf_write(buf, data, size);
}

const struct fs_ops procfs = {
    .name = "proc", .magic = 0x9fa0,
    .open = proc_open,
    .getpath = proc_getpath,
    .stat = proc_stat,
    .fstat = proc_fstat,
    .readlink = proc_readlink,
};
