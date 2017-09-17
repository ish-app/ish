#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/xattr.h>
#include <sys/file.h>

#include "sys/errno.h"
#include "sys/calls.h"
#include "sys/fs.h"
#include "fs/dev.h"
#include "fs/tty.h"

static int getpath(int fd, char *buf) {
#if defined(__linux__)
    char proc_fd[20];
    sprintf(proc_fd, "/proc/self/fd/%d", fd);
    ssize_t size = readlink(proc_fd, buf, MAX_PATH - 1);
    if (size >= 0)
        buf[size] = '\0';
    return size;
#elif defined(__APPLE__)
    return fcntl(fd, F_GEPATH, buf);
#endif
}

char *strnprepend(char *str, const char *prefix, size_t max) {
    if (strlen(str) + strlen(prefix) + 1 > max)
        return NULL;
    const char *src = str + strlen(str) + 1;
    char *dst = (char *) src + strlen(prefix);
    while (src != str)
        *dst-- = *src--;
    *dst = *src;
    src = prefix;
    dst = str;
    while (*src != '\0')
        *dst++ = *src++;
    return str;
}

// TODO translate goddamn flags

static struct fd *realfs_open(struct mount *mount, char *path, int flags, int mode) {
    if (strnprepend(path, mount->source, MAX_PATH) == NULL)
        return ERR_PTR(_ENAMETOOLONG);

    int fd_no = open(path, flags, mode);
    if (fd_no < 0)
        return ERR_PTR(err_map(errno));
    struct fd *fd = fd_create();
    fd->real_fd = fd_no;
    fd->dir = NULL;
    fd->ops = &realfs_fdops;
    return fd;
}

static int realfs_close(struct fd *fd) {
    int err;
    err = close(fd->real_fd);
    if (err < 0)
        return err_map(errno);
    if (fd->dir != NULL) {
        err = closedir(fd->dir);
        if (err < 0)
            return err_map(errno);
    }
    return 0;
}

// The values in this structure are stored in an extended attribute on a file,
// because on iOS I can't change the uid or gid of a file.
// TODO the xattr api is a little different on darwin
struct xattr_stat {
    dword_t mode;
    dword_t uid;
    dword_t gid;
    dword_t dev;
    dword_t rdev;
};
#if defined(__linux__)
#define STAT_XATTR "user.ish.stat"
#elif defined(__APPLE__)
#define STAT_XATTR "com.tbodt.ish.stat"
#endif

static void copy_stat(struct statbuf *fake_stat, struct stat *real_stat) {
    fake_stat->dev = dev_fake_from_real(real_stat->st_dev);
    fake_stat->inode = real_stat->st_ino;
    fake_stat->mode = real_stat->st_mode;
    fake_stat->nlink = real_stat->st_nlink;
    fake_stat->uid = real_stat->st_uid;
    fake_stat->gid = real_stat->st_gid;
    fake_stat->rdev = dev_fake_from_real(real_stat->st_rdev);
    fake_stat->size = real_stat->st_size;
    fake_stat->blksize = real_stat->st_blksize;
    fake_stat->blocks = real_stat->st_blocks;
    fake_stat->atime = real_stat->st_atime;
    fake_stat->mtime = real_stat->st_mtime;
    fake_stat->ctime = real_stat->st_ctime;
    // TODO this representation of nanosecond timestamps is linux-specific
    fake_stat->atime_nsec = real_stat->st_atim.tv_nsec;
    fake_stat->mtime_nsec = real_stat->st_mtim.tv_nsec;
    fake_stat->ctime_nsec = real_stat->st_ctim.tv_nsec;
}

static void copy_xattr_stat(struct statbuf *fake_stat, struct xattr_stat *xstat) {
    fake_stat->dev = xstat->dev;
    fake_stat->mode = xstat->mode;
    fake_stat->uid = xstat->uid;
    fake_stat->gid = xstat->gid;
    fake_stat->rdev = xstat->rdev;
}

static int realfs_stat(struct mount *mount, char *path, struct statbuf *fake_stat, bool follow_links) {
    if (strnprepend(path, mount->source, MAX_PATH) == NULL)
        return _ENAMETOOLONG;

    struct stat real_stat;
    int (*stat_fn)(const char *path, struct stat *buf);
    if (follow_links)
        stat_fn = stat;
    else
        stat_fn = lstat;
    if (stat_fn(path, &real_stat) < 0)
        return err_map(errno);
    copy_stat(fake_stat, &real_stat);

    struct xattr_stat xstat;
    if (lgetxattr(path, STAT_XATTR, &xstat, sizeof(xstat)) == sizeof(xstat))
        copy_xattr_stat(fake_stat, &xstat);
    return 0;
}

static int realfs_fstat(struct fd *fd, struct statbuf *fake_stat) {
    struct stat real_stat;
    if (fstat(fd->real_fd, &real_stat) < 0)
        return err_map(errno);
    copy_stat(fake_stat, &real_stat);

    struct xattr_stat xstat;
    if (fgetxattr(fd->real_fd, STAT_XATTR, &xstat, sizeof(xstat)) == sizeof(xstat))
        copy_xattr_stat(fake_stat, &xstat);
    return 0;
}

static int realfs_unlink(struct mount *mount, char *path) {
    if (strnprepend(path, mount->source, MAX_PATH) == NULL)
        return _ENAMETOOLONG;

    int res = unlink(path);
    if (res < 0)
        return err_map(errno);
    return res;
}

static int realfs_access(struct mount *mount, char *path, int mode) {
    if (strnprepend(path, mount->source, MAX_PATH) == NULL)
        return _ENAMETOOLONG;

    int real_mode = 0;
    if (mode & AC_F) real_mode |= F_OK;
    if (mode & AC_R) real_mode |= R_OK;
    if (mode & AC_W) real_mode |= W_OK;
    if (mode & AC_X) real_mode |= X_OK;
    int res = access(path, real_mode);
    if (res < 0)
        return err_map(errno);
    return res;
}

static ssize_t realfs_read(struct fd *fd, void *buf, size_t bufsize) {
    ssize_t res = read(fd->real_fd, buf, bufsize);
    if (res < 0)
        return err_map(errno);
    return res;
}

static ssize_t realfs_write(struct fd *fd, const void *buf, size_t bufsize) {
    ssize_t res = write(fd->real_fd, buf, bufsize);
    if (res < 0)
        return err_map(errno);
    return res;
}

static int realfs_readdir(struct fd *fd, struct dir_entry *entry) {
    if (fd->dir == NULL)
        fd->dir = fdopendir(fd->real_fd);
    if (fd->dir == NULL)
        return err_map(errno);
    errno = 0;
    struct dirent *dirent = readdir(fd->dir);
    if (dirent == NULL) {
        if (errno != 0)
            return err_map(errno);
        else
            return 1;
    }
    entry->inode = dirent->d_ino;
    entry->offset = dirent->d_off;
    strcpy(entry->name, dirent->d_name);
    return 0;
}

static off_t realfs_lseek(struct fd *fd, off_t offset, int whence) {
    if (whence == LSEEK_SET)
        whence = SEEK_SET;
    else if (whence == LSEEK_CUR)
        whence = SEEK_CUR;
    else if (whence == LSEEK_END)
        whence = SEEK_END;
    else
        return _EINVAL;
    off_t res = lseek(fd->real_fd, offset, whence);
    if (res < 0)
        return err_map(errno);
    return res;
}

static int realfs_mmap(struct fd *fd, off_t offset, size_t len, int prot, int flags, void **mem_out) {
    int mmap_flags = 0;
    if (flags & MMAP_PRIVATE) mmap_flags |= MAP_PRIVATE;
    if (flags & MMAP_SHARED) mmap_flags |= MAP_SHARED;
    // TODO more flags are probably needed
    void *mem = mmap(NULL, len, prot, mmap_flags, fd->real_fd, offset);
    if (mem == MAP_FAILED)
        return err_map(errno);
    *mem_out = mem;
    return 0;
}

static ssize_t realfs_readlink(struct mount *mount, char *path, char *buf, size_t bufsize) {
    if (strnprepend(path, mount->source, MAX_PATH) == NULL)
        return _ENAMETOOLONG;

    ssize_t size = readlink(path, buf, bufsize);
    if (size < 0)
        return err_map(errno);
    return size;
}

static int realfs_getpath(struct fd *fd, char *buf) {
    int err = getpath(fd->real_fd, buf);
    if (err < 0)
        return err;
    size_t source_len = strlen(fd->mount->source);
    memmove(buf, buf + source_len, MAX_PATH - source_len);
    if (*buf == '\0') {
        buf[0] = '/';
        buf[1] = '\0';
    }
    return 0;
}

static int realfs_statfs(struct mount *mount, struct statfsbuf *stat) {
    stat->type = 0x7265616c;
    stat->namelen = NAME_MAX;
    stat->bsize = PAGE_SIZE;
    return 0;
}

static int realfs_flock(struct fd *fd, int operation) {
    int real_op = 0;
    if (operation & LOCK_SH_) real_op |= LOCK_SH;
    if (operation & LOCK_EX_) real_op |= LOCK_EX;
    if (operation & LOCK_UN_) real_op |= LOCK_UN;
    if (operation & LOCK_NB_) real_op |= LOCK_NB;
    return flock(fd->real_fd, real_op);
}

const struct fs_ops realfs = {
    .open = realfs_open,
    .unlink = realfs_unlink,
    .stat = realfs_stat,
    .access = realfs_access,
    .readlink = realfs_readlink,
    .fstat = realfs_fstat,
    .statfs = realfs_statfs,
    .flock = realfs_flock,
};

const struct fd_ops realfs_fdops = {
    .read = realfs_read,
    .write = realfs_write,
    .readdir = realfs_readdir,
    .lseek = realfs_lseek,
    .mmap = realfs_mmap,
    .getpath = realfs_getpath,
    .close = realfs_close,
};
