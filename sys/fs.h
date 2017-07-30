#ifndef FS_H
#define FS_H

#include "misc.h"
#include "fs/path.h"
#include "fs/stat.h"
#include <dirent.h>

struct fd {
    unsigned refcnt;
    unsigned flags;
    const struct fd_ops *ops;
    const struct mount *mount;
    // TODO something more generic probably
    int real_fd;
    DIR *dir;
};
typedef sdword_t fd_t;
#define MAX_FD 1024 // dynamically expanding fd table coming soon:tm:

int generic_open(const char *pathname, struct fd *fd, int flags, int mode);
int generic_unlink(const char *pathname);
#define AC_R 4
#define AC_W 2
#define AC_X 1
#define AC_F 0
int generic_access(const char *pathname, int mode);
int generic_stat(const char *pathname, struct statbuf *stat, bool follow_links);
ssize_t generic_readlink(const char *pathname, char *buf, size_t bufsize);

struct mount {
    const char *point;
    const char *source;
    const struct fs_ops *fs;
    struct mount *next;
};
struct mount *mounts;

struct fs_ops {
    // the path parameter points to MAX_PATH bytes of allocated memory, which
    // you can do whatever you want with (but make sure to return _ENAMETOOLONG
    // instead of overflowing the buffer)
    int (*open)(struct mount *mount, char *path, struct fd *fd, int flags, int mode);
    int (*unlink)(struct mount *mount, char *pathname);
    int (*access)(struct mount *mount, char *path, int mode);
    int (*stat)(struct mount *mount, char *path, struct statbuf *stat, bool follow_links);
    ssize_t (*readlink)(struct mount *mount, char *path, char *buf, size_t bufsize);
};

#define NAME_MAX 255
struct dir_entry {
    qword_t inode;
    qword_t offset;
    char name[NAME_MAX + 1];
};

struct fd_ops {
    ssize_t (*read)(struct fd *fd, void *buf, size_t bufsize);
    ssize_t (*write)(struct fd *fd, void *buf, size_t bufsize);
    off_t (*lseek)(struct fd *fd, off_t off, int whence);

    // Reads a directory entry from the stream
    int (*readdir)(struct fd *fd, struct dir_entry *entry);

    // memory returned must be allocated with mmap, as it is freed with munmap
    int (*mmap)(struct fd *fd, off_t offset, size_t len, int prot, int flags, void **mem_out);
    int (*stat)(struct fd *fd, struct statbuf *stat);

    // returns the size needed for the output of ioctl, 0 if the arg is not a
    // pointer, -1 for invalid command
    ssize_t (*ioctl_size)(struct fd *fd, int cmd);
    // if ioctl_size returns non-zero, arg must point to ioctl_size valid bytes
    int (*ioctl)(struct fd *fd, int cmd, void *arg);

    int (*close)(struct fd *fd);
};

struct mount *find_mount_and_trim_path(char *path);

// real fs
extern const struct fs_ops realfs;
extern const struct fd_ops realfs_fdops; // TODO remove from header file

// TODO put this somewhere else
char *strnprepend(char *str, const char *prefix, size_t max);

#endif
