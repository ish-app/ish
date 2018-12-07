#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "kernel/fs.h"
#include "fs/fd.h"
#include "fs/path.h"
#include "fs/dev.h"
#include "kernel/task.h"
#include "kernel/errno.h"

struct mount *find_mount(char *path) {
    struct mount *mount;
    for (mount = mounts; mount != NULL; mount = mount->next)
        if (strncmp(path, mount->point, strlen(mount->point)) == 0)
            break;
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
    err = fd->mount->fs->fstat(fd, &stat);
    if (err >= 0) {
        assert(!S_ISLNK(stat.mode));
        if (S_ISBLK(stat.mode) || S_ISCHR(stat.mode)) {
            int type;
            if (S_ISBLK(stat.mode))
                type = DEV_BLOCK;
            else
                type = DEV_CHAR;
            int major = dev_major(stat.rdev);
            int minor = dev_minor(stat.rdev);
            err = dev_open(major, minor, type, fd);
            if (err < 0) {
                fd_close(fd);
                return ERR_PTR(err);
            }
        }
    }
    return fd;
}

struct fd *generic_open(const char *path, int flags, int mode) {
    return generic_openat(AT_PWD, path, flags, mode);
}

int generic_accessat(struct fd *dirfd, const char *path_raw, int mode) {
    char path[MAX_PATH];
    int err = path_normalize(dirfd, path_raw, path, true);
    if (err < 0)
        return err;

    struct mount *mount = find_mount_and_trim_path(path);
    struct statbuf stat;
    err = mount->fs->stat(mount, path, &stat, true);
    if (err < 0)
        return err;
    // TODO do an actual permissions check
    return 0;
}

int generic_linkat(struct fd *src_at, const char *src_raw, struct fd *dst_at, const char *dst_raw) {
    char src[MAX_PATH];
    int err = path_normalize(src_at, src_raw, src, false);
    if (err < 0)
        return err;
    char dst[MAX_PATH];
    err = path_normalize(dst_at, dst_raw, dst, false);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(src);
    struct mount *dst_mount = find_mount_and_trim_path(dst);
    if (mount != dst_mount)
        return _EXDEV;
    return mount->fs->link(mount, src, dst);
}

int generic_unlinkat(struct fd *at, const char *path_raw) {
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, false);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->unlink(mount, path);
}

int generic_renameat(struct fd *src_at, const char *src_raw, struct fd *dst_at, const char *dst_raw) {
    char src[MAX_PATH];
    int err = path_normalize(src_at, src_raw, src, false);
    if (err < 0)
        return err;
    char dst[MAX_PATH];
    err = path_normalize(dst_at, dst_raw, dst, false);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(src);
    struct mount *dst_mount = find_mount_and_trim_path(dst);
    if (mount != dst_mount)
        return _EXDEV;
    return mount->fs->rename(mount, src, dst);
}

int generic_symlinkat(const char *target, struct fd *at, const char *link_raw) {
    char link[MAX_PATH];
    int err = path_normalize(at, link_raw, link, false);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(link);
    return mount->fs->symlink(mount, target, link);
}

int generic_mknod(const char *path_raw, mode_t_ mode, dev_t_ dev) {
    if (S_ISDIR(mode) || S_ISLNK(mode))
        return _EINVAL;
    if (!superuser() && (S_ISBLK(mode) || S_ISCHR(mode)))
        return _EPERM;

    char path[MAX_PATH];
    int err = path_normalize(AT_PWD, path_raw, path, false);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    if (mount->fs->mknod == NULL)
        return _EPERM;
    return mount->fs->mknod(mount, path, mode, dev);
}

int generic_setattrat(struct fd *at, const char *path_raw, struct attr attr, bool follow_links) {
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, follow_links);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->setattr(mount, path, attr);
}

int generic_utime(struct fd *at, const char *path_raw, struct timespec atime, struct timespec mtime, bool follow_links) {
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, follow_links);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->utime(mount, path, atime, mtime);
}

ssize_t generic_readlinkat(struct fd *at, const char *path_raw, char *buf, size_t bufsize) {
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, false);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->readlink(mount, path, buf, bufsize);
}

int generic_mkdirat(struct fd *at, const char *path_raw, mode_t_ mode) {
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, true);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->mkdir(mount, path, mode);
}

int generic_rmdirat(struct fd *at, const char *path_raw) {
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, true);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->rmdir(mount, path);
}
