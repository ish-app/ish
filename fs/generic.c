#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sys/fs.h"
#include "fs/dev.h"
#include "sys/process.h"

struct fd *fd_create() {
    struct fd *fd = malloc(sizeof(struct fd));
    fd->refcnt = 1;
    fd->flags = 0;
    fd->mount = NULL;
    list_init(&fd->poll_fds);
    return fd;
}

static struct mount *find_mount(char *path) {
    struct mount *mount;
    for (mount = mounts; mount != NULL; mount = mount->next) {
        if (strncmp(path, mount->point, strlen(mount->point)) == 0) {
            break;
        }
    }
    assert(mount != NULL); // this would mean there's no root FS mounted
    return mount;
}

struct mount *find_mount_and_trim_path(char *path) {
    struct mount *mount = find_mount(path);
    char *dst = path;
    const char *src = path + strlen(mount->point);
    while (*src != '\0')
        *dst++ = *src++;
    *dst = '\0';
    return mount;
}

struct fd *generic_openat(struct fd *at, const char *path_raw, int flags, int mode) {
    // TODO really, really, seriously reconsider what I'm doing with the strings
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, true);
    if (err < 0)
        return ERR_PTR(err);
    struct mount *mount = find_mount_and_trim_path(path);
    struct fd *fd = mount->fs->open(mount, path, flags, mode);
    if (IS_ERR(fd))
        return fd;
    fd->mount = mount;

    struct statbuf stat;
    err = generic_fstat(fd, &stat);
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
            if (err < 0) {
                generic_close(fd);
                return ERR_PTR(err);
            }
        }
    }
    return fd;
}

struct fd *generic_open(const char *path, int flags, int mode) {
    return generic_openat(current->pwd, path, flags, mode);
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

struct fd *generic_dup(struct fd *fd) {
    fd->refcnt++;
    return fd;
}

int generic_access(const char *path_raw, int mode) {
    char path[MAX_PATH];
    int err = path_normalize(NULL, path_raw, path, true);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->access(mount, path, mode);
}

int generic_unlink(const char *path_raw) {
    char path[MAX_PATH];
    int err = path_normalize(NULL, path_raw, path, true);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->unlink(mount, path);
}

ssize_t generic_readlink(const char *path_raw, char *buf, size_t bufsize) {
    char path[MAX_PATH];
    int err = path_normalize(NULL, path_raw, path, false);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->readlink(mount, path, buf, bufsize);
}
