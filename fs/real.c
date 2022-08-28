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
#include <sys/statvfs.h>
#include <poll.h>

#include "debug.h"
#include "kernel/errno.h"
#include "kernel/calls.h"
#include "kernel/fs.h"
#include "fs/dev.h"
#include "fs/real.h"
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
    return fcntl(fd, F_GETPATH, buf);
#endif
}

// temporarily change directory and block other threads from doing so
// useful for simulating mknodat on ios, dealing with long unix socket paths, etc
lock_t fchdir_lock;
static void lock_fchdir(int dirfd) {
    lock(&fchdir_lock);
    fchdir(dirfd);
}
static void unlock_fchdir() {
    unlock(&fchdir_lock);
}

static int open_flags_real_from_fake(int flags) {
    int real_flags = 0;
    if (flags & O_RDONLY_) real_flags |= O_RDONLY;
    if (flags & O_WRONLY_) real_flags |= O_WRONLY;
    if (flags & O_RDWR_) real_flags |= O_RDWR;
    if (flags & O_CREAT_) real_flags |= O_CREAT;
    if (flags & O_TRUNC_) real_flags |= O_TRUNC;
    if (flags & O_APPEND_) real_flags |= O_APPEND;
    if (flags & O_NONBLOCK_) real_flags |= O_NONBLOCK;
    return real_flags;
}

static int open_flags_fake_from_real(int flags) {
    int fake_flags = 0;
    if (flags & O_RDONLY) fake_flags |= O_RDONLY_;
    if (flags & O_WRONLY) fake_flags |= O_WRONLY_;
    if (flags & O_RDWR) fake_flags |= O_RDWR_;
    if (flags & O_CREAT) fake_flags |= O_CREAT_;
    if (flags & O_TRUNC) fake_flags |= O_TRUNC_;
    if (flags & O_APPEND) fake_flags |= O_APPEND_;
    if (flags & O_NONBLOCK) fake_flags |= O_NONBLOCK_;
    return fake_flags;
}

struct fd *realfs_open(struct mount *mount, const char *path, int flags, int mode) {
    int real_flags = open_flags_real_from_fake(flags);
    int fd_no = openat(mount->root_fd, fix_path(path), real_flags, mode);
    if (fd_no < 0)
        return ERR_PTR(errno_map());
    struct fd *fd = fd_create(&realfs_fdops);
    fd->real_fd = fd_no;
    fd->dir = NULL;
    return fd;
}

int realfs_close(struct fd *fd) {
    if (fd->dir != NULL)
        closedir(fd->dir);
    int err = close(fd->real_fd);
    if (err < 0)
        return errno_map();
    return 0;
}

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
#if __APPLE__
#define TIMESPEC(x) st_##x##timespec
#elif __linux__
#define TIMESPEC(x) st_##x##tim
#endif
    fake_stat->atime_nsec = real_stat->TIMESPEC(a).tv_nsec;
    fake_stat->mtime_nsec = real_stat->TIMESPEC(m).tv_nsec;
    fake_stat->ctime_nsec = real_stat->TIMESPEC(c).tv_nsec;
#undef TIMESPEC
}

int realfs_stat(struct mount *mount, const char *path, struct statbuf *fake_stat) {
    struct stat real_stat;
    if (fstatat(mount->root_fd, fix_path(path), &real_stat, AT_SYMLINK_NOFOLLOW) < 0)
        return errno_map();
    copy_stat(fake_stat, &real_stat);
    return 0;
}

int realfs_fstat(struct fd *fd, struct statbuf *fake_stat) {
    struct stat real_stat;
    if (fstat(fd->real_fd, &real_stat) < 0)
        return errno_map();
    copy_stat(fake_stat, &real_stat);
    return 0;
}

ssize_t realfs_read(struct fd *fd, void *buf, size_t bufsize) {
    ssize_t res = read(fd->real_fd, buf, bufsize);
    if (res < 0)
        return errno_map();
    return res;
}

ssize_t realfs_write(struct fd *fd, const void *buf, size_t bufsize) {
    ssize_t res = write(fd->real_fd, buf, bufsize);
    if (res < 0)
        return errno_map();
    return res;
}

ssize_t realfs_pread(struct fd *fd, void *buf, size_t bufsize, off_t off) {
    ssize_t res = pread(fd->real_fd, buf, bufsize, off);
    if (res < 0)
        return errno_map();
    return res;
}

ssize_t realfs_pwrite(struct fd *fd, const void *buf, size_t bufsize, off_t off) {
    ssize_t res = pwrite(fd->real_fd, buf, bufsize, off);
    if (res < 0)
        return errno_map();
    return res;
}

void realfs_opendir(struct fd *fd) {
    if (fd->dir == NULL) {
        int dirfd = dup(fd->real_fd);
        fd->dir = fdopendir(dirfd);
        // this should never get called on a non-directory
        assert(fd->dir != NULL);
    }
}

int realfs_readdir(struct fd *fd, struct dir_entry *entry) {
    realfs_opendir(fd);
    errno = 0;
    struct dirent *dirent = readdir(fd->dir);
    if (dirent == NULL) {
        if (errno != 0)
            return errno_map();
        else
            return 0;
    }
    entry->inode = dirent->d_ino;
    strcpy(entry->name, dirent->d_name);
    return 1;
}

unsigned long realfs_telldir(struct fd *fd) {
    realfs_opendir(fd);
    return telldir(fd->dir);
}

void realfs_seekdir(struct fd *fd, unsigned long ptr) {
    realfs_opendir(fd);
    seekdir(fd->dir, ptr);
}

off_t realfs_lseek(struct fd *fd, off_t offset, int whence) {
    if (fd->dir != NULL && whence == LSEEK_SET) {
        realfs_seekdir(fd, offset);
        return offset;
    }

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
        return errno_map();
    return res;
}

int realfs_poll(struct fd *fd) {
    struct pollfd p = {.fd = fd->real_fd, .events = POLLPRI};
    // prevent POLLNVAL
    int flags = fcntl(fd->real_fd, F_GETFL, 0);
    if ((flags & O_ACCMODE) != O_WRONLY)
        p.events |= POLLIN;
    if ((flags & O_ACCMODE) != O_RDONLY)
        p.events |= POLLOUT;
    if (poll(&p, 1, 0) <= 0)
        return 0;

#if defined(__APPLE__)
    // this is the "WTF is apple smoking" section

    // https://github.com/apple/darwin-xnu/blob/a449c6a3b8014d9406c2ddbdc81795da24aa7443/bsd/kern/sys_generic.c#L1856
    if (p.revents & POLLHUP)
        p.revents |= POLLOUT;
    // apparently you can sometimes get POLLPRI on a pipe??? please ignore how much of a mess this condition is
    if (is_adhoc_fd(fd) && S_ISFIFO(fd->stat.mode))
        p.revents &= ~POLLPRI;

    if (p.revents & POLLNVAL) {
        printk("pollnval %d flags %d events %d revents %d\n", fd->real_fd, flags, p.events, p.revents);
        // Seriously, fuck Darwin. I just want to poll on POLLIN|POLLOUT|POLLPRI.
        // But if there's almost any kind of error, you just get POLLNVAL back,
        // and no information about the bits that are in fact set. So ask for each
        // separately and ignore a POLLNVAL.
        // This is no longer atomic but I don't really know what to do about that.
        int events = 0;
        static const int pollbits[] = {POLLIN, POLLOUT, POLLPRI};
        for (unsigned i = 0; i < sizeof(pollbits)/sizeof(pollbits[0]); i++) {
            p.events = pollbits[i];
            if (poll(&p, 1, 0) > 0 && !(p.revents & POLLNVAL))
                events |= p.revents;
        }
        assert(!(events & POLLNVAL));
        return events;
    }
#endif

    assert(!(p.revents & POLLNVAL));
    return p.revents;
}

int realfs_mmap(struct fd *fd, struct mem *mem, page_t start, pages_t pages, off_t offset, int prot, int flags) {
    int mmap_flags = 0;
    if (flags & MMAP_PRIVATE) mmap_flags |= MAP_PRIVATE;
    if (flags & MMAP_SHARED) mmap_flags |= MAP_SHARED;
    int mmap_prot = PROT_READ;
    if (prot & P_WRITE) mmap_prot |= PROT_WRITE;

    off_t real_offset = (offset / real_page_size) * real_page_size;
    off_t correction = offset - real_offset;
    char *memory = mmap(NULL, (pages * PAGE_SIZE) + correction,
            mmap_prot, mmap_flags, fd->real_fd, real_offset);
    return pt_map(mem, start, pages, memory, correction, prot);
}

ssize_t realfs_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize) {
    ssize_t size = readlinkat(mount->root_fd, fix_path(path), buf, bufsize);
    if (size < 0)
        return errno_map();
    return size;
}

int realfs_getpath(struct fd *fd, char *buf) {
    int err = getpath(fd->real_fd, buf);
    if (err < 0)
        return err;
    if (strcmp(fd->mount->source, "/") != 0 || strcmp(buf, "/") == 0) {
        size_t source_len = strlen(fd->mount->source);
        memmove(buf, buf + source_len, MAX_PATH - source_len);
    }
    return 0;
}

int realfs_link(struct mount *mount, const char *src, const char *dst) {
    int res = linkat(mount->root_fd, fix_path(src), mount->root_fd, fix_path(dst), 0);
    if (res < 0)
        return errno_map();
    return res;
}

int realfs_unlink(struct mount *mount, const char *path) {
    int res = unlinkat(mount->root_fd, fix_path(path), 0);
    if (res < 0)
        return errno_map();
    return res;
}

int realfs_rmdir(struct mount *mount, const char *path) {
    int err = unlinkat(mount->root_fd, fix_path(path), AT_REMOVEDIR);
    if (err < 0)
        return errno_map();
    return 0;
}

int realfs_rename(struct mount *mount, const char *src, const char *dst) {
    int err = renameat(mount->root_fd, fix_path(src), mount->root_fd, fix_path(dst));
    if (err < 0)
        return errno_map();
    return err;
}

int realfs_symlink(struct mount *mount, const char *target, const char *link) {
    int err = symlinkat(target, mount->root_fd, link);
    if (err < 0)
        return errno_map();
    return err;
}

int realfs_mknod(struct mount *mount, const char *path, mode_t_ mode, dev_t_ UNUSED(dev)) {
    int err;
    if (S_ISFIFO(mode)) {
        lock_fchdir(mount->root_fd);
        err = mkfifo(fix_path(path), mode & ~S_IFMT);
        unlock_fchdir();
    } else if (S_ISREG(mode)) {
        err = openat(mount->root_fd, fix_path(path), O_CREAT|O_EXCL|O_RDONLY, mode & ~S_IFMT);
        if (err >= 0)
            err = close(err);
    } else {
        return _EPERM;
    }
    if (err < 0)
        return errno_map();
    return err;
}

int realfs_truncate(struct mount *mount, const char *path, off_t_ size) {
    int fd = openat(mount->root_fd, fix_path(path), O_RDWR);
    if (fd < 0)
        return errno_map();
    int err = 0;
    if (ftruncate(fd, size) < 0)
        err = errno_map();
    close(fd);
    return err;
}

int realfs_setattr(struct mount *mount, const char *path, struct attr attr) {
    path = fix_path(path);
    int root = mount->root_fd;
    int err;
    switch (attr.type) {
        case attr_uid:
            err = fchownat(root, path, attr.uid, -1, 0);
            break;
        case attr_gid:
            err = fchownat(root, path, attr.gid, -1, 0);
            break;
        case attr_mode:
            err = fchmodat(root, path, attr.mode, 0);
            break;
        case attr_size:
            return realfs_truncate(mount, path, attr.size);
        default:
            TODO("other attrs");
    }
    if (err < 0)
        return errno_map();
    return err;
}

int realfs_fsetattr(struct fd *fd, struct attr attr) {
    int real_fd = fd->real_fd;
    int err;
    switch (attr.type) {
        case attr_uid:
            err = fchown(real_fd, attr.uid, -1);
            break;
        case attr_gid:
            err = fchown(real_fd, attr.gid, -1);
            break;
        case attr_mode:
            err = fchmod(real_fd, attr.mode);
            break;
        case attr_size:
            err = ftruncate(real_fd, attr.size);
            break;
        default: abort();
    }
    if (err < 0)
        return errno_map();
    return err;
}

int realfs_utime(struct mount *mount, const char *path, struct timespec atime, struct timespec mtime) {
    struct timespec times[2] = {atime, mtime};
    int err = utimensat(mount->root_fd, fix_path(path), times, 0);
    if (err < 0)
        return errno_map();
    return 0;
}

int realfs_mkdir(struct mount *mount, const char *path, mode_t_ mode) {
    int err = mkdirat(mount->root_fd, fix_path(path), mode);
    if (err < 0)
        return errno_map();
    return 0;
}

int realfs_flock(struct fd *fd, int operation) {
    int real_op = 0;
    if (operation & LOCK_SH_) real_op |= LOCK_SH;
    if (operation & LOCK_EX_) real_op |= LOCK_EX;
    if (operation & LOCK_UN_) real_op |= LOCK_UN;
    if (operation & LOCK_NB_) real_op |= LOCK_NB;
    return flock(fd->real_fd, real_op);
}

int realfs_statfs(struct mount *mount, struct statfsbuf *stat) {
    struct statvfs vfs = {};
    fstatvfs(mount->root_fd, &vfs);
    stat->bsize = vfs.f_bsize;
    stat->blocks = vfs.f_blocks;
    stat->bfree = vfs.f_bfree;
    stat->bavail = vfs.f_bavail;
    stat->files = vfs.f_files;
    stat->ffree = vfs.f_ffree;
    stat->namelen = vfs.f_namemax;
    stat->frsize = vfs.f_frsize;
    return 0;
}

int realfs_mount(struct mount *mount) {
    char *source_realpath = realpath(mount->source, NULL);
    if (source_realpath == NULL)
        return errno_map();
    free((void *) mount->source);
    mount->source = source_realpath;

    mount->root_fd = open(mount->source, O_DIRECTORY);
    if (mount->root_fd < 0)
        return errno_map();
    return 0;
}

int realfs_fsync(struct fd *fd) {
    int err = fsync(fd->real_fd);
    if (err < 0)
        return errno_map();
    return 0;
}

int realfs_getflags(struct fd *fd) {
    int flags = fcntl(fd->real_fd, F_GETFL);
    if (flags < 0)
        return errno_map();
    return open_flags_fake_from_real(flags);
}

int realfs_setflags(struct fd *fd, dword_t flags) {
    int ret = fcntl(fd->real_fd, F_SETFL, open_flags_real_from_fake(flags));
    if (ret < 0)
        return errno_map();
    return 0;
}

ssize_t realfs_ioctl_size(int cmd) {
    if (cmd == FIONREAD_)
        return sizeof(dword_t);
    return -1;
}

int realfs_ioctl(struct fd *fd, int cmd, void *arg) {
    int err;
    size_t nread;
    switch (cmd) {
        case FIONREAD_:
            err = ioctl(fd->real_fd, FIONREAD, &nread);
            if (err < 0)
                return errno_map();
            *(dword_t *) arg = nread;
            return 0;
    }
    return _ENOTTY;
}

const struct fs_ops realfs = {
    .name = "real", .magic = 0x7265616c,
    .mount = realfs_mount,
    .statfs = realfs_statfs,

    .open = realfs_open,
    .readlink = realfs_readlink,
    .link = realfs_link,
    .unlink = realfs_unlink,
    .rmdir = realfs_rmdir,
    .rename = realfs_rename,
    .symlink = realfs_symlink,
    .mknod = realfs_mknod,

    .close = realfs_close,
    .stat = realfs_stat,
    .fstat = realfs_fstat,
    .setattr = realfs_setattr,
    .fsetattr = realfs_fsetattr,
    .utime = realfs_utime,
    .getpath = realfs_getpath,
    .flock = realfs_flock,

    .mkdir = realfs_mkdir,
};

const struct fd_ops realfs_fdops = {
    .read = realfs_read,
    .write = realfs_write,
    .pread = realfs_pread,
    .pwrite = realfs_pwrite,
    .readdir = realfs_readdir,
    .telldir = realfs_telldir,
    .seekdir = realfs_seekdir,
    .lseek = realfs_lseek,
    .mmap = realfs_mmap,
    .poll = realfs_poll,
    .ioctl_size = realfs_ioctl_size,
    .ioctl = realfs_ioctl,
    .fsync = realfs_fsync,
    .close = realfs_close,
    .getflags = realfs_getflags,
    .setflags = realfs_setflags,
};
