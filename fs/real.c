#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "sys/errno.h"
#include "sys/calls.h"
#include "fs/fs.h"

// TODO translate goddamn flags

static int realfs_open(char *path, struct fd *fd, int flags) {
    int fd_no = open(path, flags);
    if (fd_no < 0)
        return err_map(errno);
    fd->real_fd = fd_no;
    fd->ops = &realfs_fdops;
    return 0;
}

static int realfs_close(struct fd *fd) {
    int err = close(fd->real_fd);
    if (err < 0)
        return err_map(errno);
    return 0;
}

static void copy_stat(struct statbuf *fake_stat, struct stat *real_stat) {
    fake_stat->dev = real_stat->st_dev;
    fake_stat->inode = real_stat->st_ino;
    fake_stat->mode = real_stat->st_mode;
    fake_stat->nlink = real_stat->st_nlink;
    fake_stat->uid = real_stat->st_uid;
    fake_stat->gid = real_stat->st_gid;
    fake_stat->rdev = real_stat->st_rdev;
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

static int realfs_stat(const char *path, struct statbuf *fake_stat) {
    struct stat real_stat;
    if (stat(path, &real_stat) < 0)
        return err_map(errno);
    copy_stat(fake_stat, &real_stat);
    return 0;
}

static int realfs_fstat(struct fd *fd, struct statbuf *fake_stat) {
    struct stat real_stat;
    if (fstat(fd->real_fd, &real_stat) < 0)
        return err_map(errno);
    copy_stat(fake_stat, &real_stat);
    return 0;
}

static int realfs_access(const char *path, int mode) {
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

static ssize_t realfs_read(struct fd *fd, char *buf, size_t bufsize) {
    ssize_t res = read(fd->real_fd, buf, bufsize);
    if (res < 0)
        return err_map(errno);
    return res;
}

static ssize_t realfs_write(struct fd *fd, char *buf, size_t bufsize) {
    ssize_t res = write(fd->real_fd, buf, bufsize);
    if (res < 0)
        return err_map(errno);
    return res;
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
}

static int realfs_mmap(struct fd *fd, off_t offset, size_t len, int prot, int flags, void **mem_out) {
    int mmap_flags = 0;
    if (flags & MMAP_PRIVATE) mmap_flags |= MAP_PRIVATE;
    // TODO more flags are probably needed
    void *mem = mmap(NULL, len, prot, mmap_flags, fd->real_fd, offset);
    if (mem == MAP_FAILED)
        return err_map(errno);
    *mem_out = mem;
    return 0;
}

struct win_size {
    word_t row;
    word_t col;
    word_t xpixel;
    word_t ypixel;
};

static ssize_t realfs_ioctl_size(struct fd *fd, int cmd) {
    switch (cmd) {
        case TIOCGWINSZ: return sizeof(struct win_size);
    }
    return -1;
}

static int realfs_ioctl(struct fd *fd, int cmd, void *arg) {
    int res = ioctl(fd->real_fd, cmd, arg);
    if (res < 0)
        return err_map(errno);
    return res;
}

static ssize_t realfs_readlink(char *path, char *buf, size_t bufsize) {
    ssize_t size = readlink(path, buf, bufsize);
    if (size < 0)
        return err_map(errno);
    return size;
}

const struct fs_ops realfs = {
    .open = realfs_open,
    .stat = realfs_stat,
    .access = realfs_access,
    .readlink = realfs_readlink,
};

const struct fd_ops realfs_fdops = {
    .read = realfs_read,
    .write = realfs_write,
    .lseek = realfs_lseek,
    .mmap = realfs_mmap,
    .stat = realfs_fstat,
    .ioctl_size = realfs_ioctl_size,
    .ioctl = realfs_ioctl,
    .close = realfs_close,
};
