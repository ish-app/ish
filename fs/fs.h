#ifndef FS_H
#define FS_H

#include "misc.h"
#include "fs/path.h"

struct fd {
    unsigned refcnt;
    const struct fs_ops *fs;
    int real_fd;
};
typedef sdword_t fd_t;
#define MAX_FD 1024 // dynamically expanding fd table coming soon:tm:

int generic_open(const char *pathname, struct fd *fd, int flags);
int generic_close(struct fd *fd);
ssize_t generic_readlink(const char *pathname, char *buf, size_t bufsize);

struct mount {
    const char *mount_point;
    const struct fs_ops *fs;
    struct mount *next;
};
struct mount *mounts;

struct fs_ops {
    int (*open)(char *path, struct fd *fd, int flags);
    int (*close)(struct fd *fd);
    ssize_t (*read)(struct fd *fd, char *buf, size_t bufsize);
    ssize_t (*write)(struct fd *fd, char *buf, size_t bufsize);
    ssize_t (*readlink)(char *path, char *buf, size_t bufsize);
};

// real fs
int realfs_open(char *path, struct fd *fd, int flags);
int realfs_close(struct fd *fd);
ssize_t realfs_read(struct fd *fd, char *buf, size_t bufsize);
ssize_t realfs_write(struct fd *fd, char *buf, size_t bufsize);
ssize_t realfs_readlink(char *path, char *buf, size_t bufsize);
extern const struct fs_ops realfs;

// stopgap until I figure out something sane to do here
void mount_root();

#endif
