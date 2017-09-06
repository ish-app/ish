#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sys/errno.h"
#include "sys/fs.h"
#include "fs/dev.h"
#include "sys/process.h"

// XXX errors from generic_close aren't handled, mostly because then it would look annoying

static struct fd *dir_lookup(struct fd *at, const char *path) {
    if (*path == '\0')
        return ERR_PTR(_ENOENT);

    struct fd *fd;
    if (*path == '/')
        fd = generic_dup(current->root);
    else
        fd = generic_dup(at);

    char component[MAX_NAME+1];
    while (*path == '/')
        path++;
    while (true) {
        int i = 0;
        while (*path != '\0' && *path != '/') {
            if (i >= MAX_NAME)
                return ERR_PTR(_ENAMETOOLONG);
            component[i++] = *path++;
        }
        while (*path == '/')
            path++;
        if (*path == '\0')
            return fd;
        component[i] = '\0';

        struct fd *next_fd = generic_lookup(fd, component, 0);
        generic_close(fd);
        if (IS_ERR(next_fd))
            return next_fd;
        fd = next_fd;
    }
}

struct fd *dir_open(struct fd *at, const char *path, const char **file) {
    struct fd *fd = dir_lookup(at, path);
    if (IS_ERR(fd))
        return fd;
    *file = path;
    while (*path != '\0') {
        if (*path == '/') {
            while (*path == '/')
                path++;
            if (*path != '\0')
                *file = path;
        }
        path++;
    }
    return fd;
}

struct fd *generic_lookup(struct fd *dir, const char *name, int flags) {
    if (*name == '\0')
        return dir;

    char buf[MAX_PATH];
    ssize_t size = dir->fs->readlink(dir, name, buf, sizeof(buf) - 1);
    if (size >= 0) {
        buf[size] = '\0';
        const char *file;
        struct fd *new_dir = dir_open(dir, buf, &file);
        if (IS_ERR(new_dir))
            return new_dir;
        return generic_lookup(new_dir, file, flags);
    }
    return dir->fs->lookup(dir, name, flags);
}

struct fd *generic_open(const char *path, int flags, int mode) {
    // TODO O_DIRECTORY_

    const char *file;
    struct fd *dir = dir_open(current->pwd, path, &file);
    if (IS_ERR(dir))
        return dir;
    struct fd *fd = generic_lookup(dir, file, flags);
    generic_close(dir);
    if (IS_ERR(fd))
        return fd;

    struct statbuf stat;
    int err = generic_fstat(fd, &stat);
    if (err >= 0) {
        int type = stat.mode & S_IFMT;
        if (type == S_IFBLK || type == S_IFCHR) {
            if (stat.mode & S_IFBLK)
                type = DEV_BLOCK;
            else
                type = DEV_CHAR;
            int major = dev_major(stat.rdev);
            int minor = dev_minor(stat.rdev);
            err = dev_open(major, minor, type, fd);
            if (err < 0)
                return ERR_PTR(err);
        }
    }
    return fd;
}

int generic_close(struct fd *fd) {
    if (--fd->refcnt == 0) {
        int err = fd->ops->close(fd);
        if (err < 0)
            return err;
        free(fd);
    }
    return 0;
}

// hmm
struct fd *generic_dup(struct fd *fd) {
    fd->refcnt++;
    return fd;
}

int generic_access(const char *path, int mode) {
    const char *file;
    struct fd *dir = dir_open(current->pwd, path, &file);
    if (IS_ERR(dir))
        return PTR_ERR(dir);
    int err = dir->fs->access(dir, file, mode);
    generic_close(dir);
    return err;
}

int generic_unlink(const char *path) {
    const char *file;
    struct fd *dir = dir_open(current->pwd, path, &file);
    if (IS_ERR(dir))
        return PTR_ERR(dir);
    int err = dir->fs->unlink(dir, file);
    generic_close(dir);
    return err;
}

ssize_t generic_readlink(const char *path, char *buf, size_t bufsize) {
    const char *file;
    struct fd *dir = dir_open(current->pwd, path, &file);
    if (IS_ERR(dir))
        return PTR_ERR(dir);
    ssize_t err = dir->fs->readlink(dir, file, buf, bufsize);
    generic_close(dir);
    return err;
}

int generic_stat(const char *path, struct statbuf *stat, bool follow_links) {
    const char *file;
    struct fd *dir = dir_open(current->pwd, path, &file);
    if (IS_ERR(dir))
        return PTR_ERR(dir);
    int err = dir->fs->stat(dir, file, stat, follow_links);
    generic_close(dir);
    return err;
}

int generic_fstat(struct fd *fd, struct statbuf *stat) {
    if (fd->mount) {
        return fd->fs->fstat(fd, stat);
    } else {
        memcpy(stat, fd->stat, sizeof(*stat));
        return 0;
    }
}

