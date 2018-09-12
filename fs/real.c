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

#include "kernel/errno.h"
#include "kernel/calls.h"
#include "kernel/fs.h"
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
    return fcntl(fd, F_GETPATH, buf);
#endif
}

const char *fix_path(const char *path) {
    if (strcmp(path, "") == 0)
        return ".";
    if (path[0] == '/')
        path++;
    return path;
}

static struct fd *realfs_open(struct mount *mount, const char *path, int flags, int mode) {
    int real_flags = 0;
    if (flags & O_RDONLY_) real_flags |= O_RDONLY;
    if (flags & O_WRONLY_) real_flags |= O_WRONLY;
    if (flags & O_RDWR_) real_flags |= O_RDWR;
    if (flags & O_CREAT_) real_flags |= O_CREAT;
    int fd_no = openat(mount->root_fd, fix_path(path), real_flags, mode);
    if (fd_no < 0)
        return ERR_PTR(errno_map());
    struct fd *fd = fd_create();
    fd->real_fd = fd_no;
    fd->dir = NULL;
    fd->ops = &realfs_fdops;
    return fd;
}

int realfs_close(struct fd *fd) {
    if (fd->dir != NULL)
        closedir(fd->dir);
    int err = close(fd->real_fd);
    if (err < 0)
        err = errno_map();
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
    // TODO this representation of nanosecond timestamps is linux-specific
    /* fake_stat->atime_nsec = real_stat->st_atim.tv_nsec; */
    /* fake_stat->mtime_nsec = real_stat->st_mtim.tv_nsec; */
    /* fake_stat->ctime_nsec = real_stat->st_ctim.tv_nsec; */
}

static int realfs_stat(struct mount *mount, const char *path, struct statbuf *fake_stat, bool follow_links) {
    struct stat real_stat;
    if (fstatat(mount->root_fd, fix_path(path), &real_stat, follow_links ? 0 : AT_SYMLINK_NOFOLLOW) < 0)
        return errno_map();
    copy_stat(fake_stat, &real_stat);
    return 0;
}

static int realfs_fstat(struct fd *fd, struct statbuf *fake_stat) {
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

int realfs_readdir(struct fd *fd, struct dir_entry *entry) {
    if (fd->dir == NULL)
        fd->dir = fdopendir(dup(fd->real_fd));
    if (fd->dir == NULL)
        return errno_map();
    errno = 0;
    struct dirent *dirent = readdir(fd->dir);
    if (dirent == NULL) {
        if (errno != 0)
            return errno_map();
        else
            return 1;
    }
    entry->inode = dirent->d_ino;
    /* entry->offset = dirent->d_off; */
    strcpy(entry->name, dirent->d_name);
    return 0;
}

off_t realfs_lseek(struct fd *fd, off_t offset, int whence) {
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

int realfs_mmap(struct fd *fd, struct mem *mem, page_t start, pages_t pages, off_t offset, int prot, int flags) {
    if (pages == 0)
        return 0;

    int mmap_flags = 0;
    if (flags & MMAP_PRIVATE) mmap_flags |= MAP_PRIVATE;
    if (flags & MMAP_SHARED) mmap_flags |= MAP_SHARED;
    int mmap_prot = PROT_READ;
    if (prot & P_WRITE) mmap_prot |= PROT_WRITE;

    off_t real_offset = (offset / real_page_size) * real_page_size;
    off_t correction = offset - real_offset;
    char *memory = mmap(NULL, (pages * PAGE_SIZE) + correction,
            mmap_prot, mmap_flags, fd->real_fd, real_offset);
    if (memory != MAP_FAILED)
        memory += correction;
    return pt_map(mem, start, pages, memory, prot);
}

static ssize_t realfs_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize) {
    ssize_t size = readlinkat(mount->root_fd, fix_path(path), buf, bufsize);
    if (size < 0)
        return errno_map();
    return size;
}

int realfs_getpath(struct fd *fd, char *buf) {
    int err = getpath(fd->real_fd, buf);
    if (err < 0)
        return err;
    if (strcmp(fd->mount->source, "/") != 0) {
        size_t source_len = strlen(fd->mount->source);
        memmove(buf, buf + source_len, MAX_PATH - source_len);
    }
    return 0;
}

static int realfs_link(struct mount *mount, const char *src, const char *dst) {
    int res = linkat(mount->root_fd, fix_path(src), mount->root_fd, fix_path(dst), 0);
    if (res < 0)
        return errno_map();
    return res;
}

static int realfs_unlink(struct mount *mount, const char *path) {
    int res = unlinkat(mount->root_fd, fix_path(path), 0);
    if (res < 0)
        return errno_map();
    return res;
}

static int realfs_rmdir(struct mount *mount, const char *path) {
    int err = unlinkat(mount->root_fd, fix_path(path), AT_REMOVEDIR);
    if (err < 0)
        return errno_map();
    return 0;
}

static int realfs_rename(struct mount *mount, const char *src, const char *dst) {
    int err = renameat(mount->root_fd, fix_path(src), mount->root_fd, fix_path(dst));
    if (err < 0)
        return errno_map();
    return err;
}

static int realfs_symlink(struct mount *mount, const char *target, const char *link) {
    int err = symlinkat(target, mount->root_fd, link);
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
    return 0;
}

static int realfs_setattr(struct mount *mount, const char *path, struct attr attr) {
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

static int realfs_fsetattr(struct fd *fd, struct attr attr) {
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

static int realfs_mkdir(struct mount *mount, const char *path, mode_t_ mode) {
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
    stat->type = 0x7265616c;
    stat->namelen = NAME_MAX;
    stat->bsize = PAGE_SIZE;
    return 0;
}

int realfs_mount(struct mount *mount) {
    mount->root_fd = open(mount->source, O_DIRECTORY);
    if (mount->root_fd < 0)
        return errno_map();
    return 0;
}

const struct fs_ops realfs = {
    .mount = realfs_mount,
    .statfs = realfs_statfs,
    .open = realfs_open,
    .readlink = realfs_readlink,
    .link = realfs_link,
    .unlink = realfs_unlink,
    .rmdir = realfs_rmdir,
    .rename = realfs_rename,
    .symlink = realfs_symlink,

    .stat = realfs_stat,
    .fstat = realfs_fstat,
    .setattr = realfs_setattr,
    .fsetattr = realfs_fsetattr,
    .utime = realfs_utime,
    .flock = realfs_flock,

    .mkdir = realfs_mkdir,
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
