#ifndef FS_REAL_H
#define FS_REAL_H

#include "kernel/fs.h"

extern const struct fd_ops realfs_fdops;
extern const struct fs_ops realfs;

struct fd *realfs_open(struct mount *mount, const char *path, int flags, int mode);

ssize_t realfs_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize);
int realfs_link(struct mount *mount, const char *src, const char *dst);
int realfs_unlink(struct mount *mount, const char *path);
int realfs_rmdir(struct mount *mount, const char *path);
int realfs_rename(struct mount *mount, const char *src, const char *dst);
int realfs_symlink(struct mount *mount, const char *target, const char *link);
int realfs_mknod(struct mount *mount, const char *path, mode_t_ mode, dev_t_ UNUSED(dev));

int realfs_stat(struct mount *mount, const char *path, struct statbuf *fake_stat);
int realfs_statfs(struct mount *mount, struct statfsbuf *stat);
int realfs_fstat(struct fd *fd, struct statbuf *fake_stat);
int realfs_setattr(struct mount *mount, const char *path, struct attr attr);
int realfs_fsetattr(struct fd *fd, struct attr attr);

int realfs_mkdir(struct mount *mount, const char *path, mode_t_ mode);

int realfs_truncate(struct mount *mount, const char *path, off_t_ size);
int realfs_utime(struct mount *mount, const char *path, struct timespec atime, struct timespec mtime);

int realfs_statfs(struct mount *mount, struct statfsbuf *stat);
int realfs_flock(struct fd *fd, int operation);
int realfs_getpath(struct fd *fd, char *buf);
ssize_t realfs_read(struct fd *fd, void *buf, size_t bufsize);
ssize_t realfs_write(struct fd *fd, const void *buf, size_t bufsize);

int realfs_readdir(struct fd *fd, struct dir_entry *entry);
unsigned long realfs_telldir(struct fd *fd);
void realfs_seekdir(struct fd *fd, unsigned long ptr);

off_t realfs_lseek(struct fd *fd, off_t offset, int whence);

int realfs_poll(struct fd *fd);
int realfs_mmap(struct fd *fd, struct mem *mem, page_t start, pages_t pages, off_t offset, int prot, int flags);
int realfs_fsync(struct fd *fd);
int realfs_getflags(struct fd *fd);
int realfs_setflags(struct fd *fd, dword_t arg);
ssize_t realfs_ioctl_size(int cmd);
int realfs_ioctl(struct fd *fd, int cmd, void *arg);
int realfs_close(struct fd *fd);

#endif
