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

int generic_open(const char *pathname, struct fd *fd, int flags);
#define AC_R 4
#define AC_W 2
#define AC_X 1
#define AC_F 0
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
    int (*open)(char *path, struct fd *fd, int flags);
    int (*access)(const char *path, int mode);
    int (*stat)(const char *path, struct statbuf *stat);
    ssize_t (*readlink)(char *path, char *buf, size_t bufsize);
};

struct fd_ops {
    ssize_t (*read)(struct fd *fd, char *buf, size_t bufsize);
    ssize_t (*write)(struct fd *fd, char *buf, size_t bufsize);
    int (*mmap)(struct fd *fd, off_t offset, size_t len, int prot, int flags, void **mem_out);
    int (*stat)(struct fd *fd, struct statbuf *stat);
    int (*close)(struct fd *fd);
};

// real fs
extern const struct fs_ops realfs;
extern const struct fd_ops realfs_fdops; // TODO remove from header file

// stopgap until I figure out something sane to do here
void mount_root();

#endif
