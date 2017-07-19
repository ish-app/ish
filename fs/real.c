#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/xattr.h>

#include "sys/errno.h"
#include "sys/calls.h"
#include "sys/fs.h"

// TODO translate goddamn flags

static int realfs_open(char *path, struct fd *fd, int flags, int mode) {
    /* debugger; */
    int fd_no = open(path, flags, mode);
    if (fd_no < 0)
        return err_map(errno);
    fd->real_fd = fd_no;
    fd->dir = NULL;
    fd->ops = &realfs_fdops;
    return 0;
}

static int realfs_close(struct fd *fd) {
    int err;
    if (fd->dir != NULL)
        err = closedir(fd->dir);
    else
        err = close(fd->real_fd);
    if (err < 0)
        return err_map(errno);
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

static void copy_xattr_stat(struct statbuf *fake_stat, struct xattr_stat *xstat) {
    fake_stat->dev = xstat->dev;
    fake_stat->mode = xstat->mode;
    fake_stat->uid = xstat->uid;
    fake_stat->gid = xstat->gid;
    fake_stat->rdev = xstat->rdev;
}

static int realfs_stat(const char *path, struct statbuf *fake_stat, bool follow_links) {
    struct stat real_stat;
    if ((follow_links ? lstat : stat)(path, &real_stat) < 0)
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

static int realfs_unlink(const char *path) {
    int res = unlink(path);
    if (res < 0)
        return err_map(errno);
    return res;
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

// This is the definition of __kernel_termios from glibc
struct termios_ {
    dword_t iflags;
    dword_t oflags;
    dword_t cflags;
    dword_t lflags;
    byte_t cc[19];
    byte_t line;
};

#define TCGETS_ 0x5401
#define TCSETS_ 0x5402
#define TIOCGWINSZ_ 0x5413

static ssize_t realfs_ioctl_size(struct fd *fd, int cmd) {
    switch (cmd) {
        case TIOCGWINSZ_: return sizeof(struct win_size);
        case TCGETS_: return sizeof(struct termios_);
        case TCSETS_: return sizeof(struct termios_);
    }
    return -1;
}

static int realfs_ioctl(struct fd *fd, int cmd, void *arg) {
    int res;
    struct winsize winsz;
    struct win_size *winsz_ = arg;
    struct termios termios;
    struct termios_ *termios_ = arg;
    switch (cmd) {
        case TIOCGWINSZ_:
            if (!(res = ioctl(fd->real_fd, TIOCGWINSZ, &winsz))) {
                winsz_->col = winsz.ws_col;
                winsz_->row = winsz.ws_row;
                winsz_->xpixel = winsz.ws_xpixel;
                winsz_->ypixel = winsz.ws_ypixel;
            }
            break;
        case TCGETS_:
            if (!(res = tcgetattr(fd->real_fd, &termios))) {
                termios_->iflags = termios.c_iflag;
                termios_->oflags = termios.c_oflag;
                termios_->cflags = termios.c_cflag;
                termios_->lflags = termios.c_lflag;
                termios_->line = termios.c_line;
                memcpy(&termios_->cc, &termios.c_cc, sizeof(termios_->cc));
            }
            break;
        case TCSETS_:
            termios.c_iflag = termios_->iflags;
            termios.c_oflag = termios_->oflags;
            termios.c_cflag = termios_->cflags;
            termios.c_lflag = termios_->lflags;
            termios.c_line = termios_->line;
            memcpy(&termios.c_cc, &termios_->cc, sizeof(termios_->cc));
            res = tcsetattr(fd->real_fd, TCSANOW, &termios);
            break;
    }
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
    .unlink = realfs_unlink,
    .stat = realfs_stat,
    .access = realfs_access,
    .readlink = realfs_readlink,
};

const struct fd_ops realfs_fdops = {
    .read = realfs_read,
    .write = realfs_write,
    .readdir = realfs_readdir,
    .lseek = realfs_lseek,
    .mmap = realfs_mmap,
    .stat = realfs_fstat,
    .ioctl_size = realfs_ioctl_size,
    .ioctl = realfs_ioctl,
    .close = realfs_close,
};
