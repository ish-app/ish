#ifndef FS_H
#define FS_H

#include "misc.h"
#include "fs/path.h"
#include "fs/stat.h"

struct fd {
    unsigned refcnt;
    const struct fd_ops *ops;
    int real_fd;
};
typedef sdword_t fd_t;
#define MAX_FD 1024 // dynamically expanding fd table coming soon:tm:

void pathname_normalize(char *pathname);
char *pathname_expand(const char *pathname);

int generic_open(const char *pathname, struct fd *fd, int flags, int mode);
#define AC_R 4
#define AC_W 2
#define AC_X 1
#define AC_F 0
int generic_unlink(const char *pathname);
int generic_access(const char *pathname, int mode);
int generic_stat(const char *pathname, struct statbuf *stat);
ssize_t generic_readlink(const char *pathname, char *buf, size_t bufsize);

struct mount {
    const char *mount_point;
    const struct fs_ops *fs;
    struct mount *next;
};
struct mount *mounts;

struct fs_ops {
    int (*open)(char *path, struct fd *fd, int flags, int mode);
    int (*unlink)(const char *pathname);
    int (*access)(const char *path, int mode);
    int (*stat)(const char *path, struct statbuf *stat);
    ssize_t (*readlink)(char *path, char *buf, size_t bufsize);
};

struct fd_ops {
    ssize_t (*read)(struct fd *fd, char *buf, size_t bufsize);
    ssize_t (*write)(struct fd *fd, char *buf, size_t bufsize);
    off_t (*lseek)(struct fd *fd, off_t off, int whence);
    int (*mmap)(struct fd *fd, off_t offset, size_t len, int prot, int flags, void **mem_out);
    int (*stat)(struct fd *fd, struct statbuf *stat);
    // returns the size needed for the output of ioctl, 0 if the arg is not a
    // pointer, -1 for invalid command
    ssize_t (*ioctl_size)(struct fd *fd, int cmd);
    // if ioctl_size returns non-zero, arg must point to ioctl_size valid bytes
    int (*ioctl)(struct fd *fd, int cmd, void *arg);
    int (*close)(struct fd *fd);
};

path_t find_mount(char *pathname, const struct fs_ops **fs);

// real fs
extern const struct fs_ops realfs;
extern const struct fd_ops realfs_fdops; // TODO remove from header file

// stopgap until I figure out something sane to do here
void mount_root();

#endif
