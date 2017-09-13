#ifndef FS_H
#define FS_H

#include "misc.h"
#include "util/list.h"
#include "fs/path.h"
#include "fs/stat.h"
#include <dirent.h>

struct poll;

struct fd {
    unsigned refcnt;
    unsigned flags;
    const struct fd_ops *ops;

    struct list poll_fds;
    struct pollable *pollable;
    struct list pollable_other_fds;

    union {
        struct {
            DIR *dir;
        };
        struct tty *tty;
    };

    // "inode"
    struct mount *mount;
    union {
        int real_fd;
        struct statbuf *stat;
    };

    pthread_mutex_t lock;
};
typedef sdword_t fd_t;
fd_t find_fd();
fd_t create_fd();
#define MAX_FD 1024 // dynamically expanding fd table coming soon:tm:

int generic_open(const char *pathname, struct fd *fd, int flags, int mode);
int generic_unlink(const char *pathname);
#define AC_R 4
#define AC_W 2
#define AC_X 1
#define AC_F 0
int generic_access(const char *pathname, int mode);
int generic_stat(const char *pathname, struct statbuf *stat, bool follow_links);
int generic_fstat(struct fd *fd, struct statbuf *stat);
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
    // i'm considering removing stat, and just having fstat, which would then be called stat
    int (*fstat)(struct fd *fd, struct statbuf *stat);
};

#define NAME_MAX 255
struct dir_entry {
    qword_t inode;
    qword_t offset;
    char name[NAME_MAX + 1];
};

struct fd_ops {
    ssize_t (*read)(struct fd *fd, void *buf, size_t bufsize);
    ssize_t (*write)(struct fd *fd, const void *buf, size_t bufsize);
    off_t (*lseek)(struct fd *fd, off_t off, int whence);

    // Reads a directory entry from the stream
    int (*readdir)(struct fd *fd, struct dir_entry *entry);

    // memory returned must be allocated with mmap, as it is freed with munmap
    int (*mmap)(struct fd *fd, off_t offset, size_t len, int prot, int flags, void **mem_out);

    // returns a bitmask of operations that won't block
    int (*poll)(struct fd *fd);

    // returns the size needed for the output of ioctl, 0 if the arg is not a
    // pointer, -1 for invalid command
    ssize_t (*ioctl_size)(struct fd *fd, int cmd);
    // if ioctl_size returns non-zero, arg must point to ioctl_size valid bytes
    int (*ioctl)(struct fd *fd, int cmd, void *arg);

    int (*close)(struct fd *fd);
};

struct mount *find_mount_and_trim_path(char *path);

struct pollable {
    struct list fds;
    pthread_mutex_t lock;
};

struct poll {
    struct list poll_fds;
    int notify_pipe[2];
    pthread_mutex_t lock;
};

struct poll_fd {
    // locked by containing struct poll
    struct fd *fd;
    struct list fds;
    int types;

    // locked by containing struct fd
    struct poll *poll;
    struct list polls;
};

#define POLL_READ 1
#define POLL_WRITE 4
struct poll_event {
    struct fd *fd;
    int types;
};
struct poll *poll_create();
int poll_add_fd(struct poll *poll, struct fd *fd, int types);
int poll_del_fd(struct poll *poll, struct fd *fd);
void poll_wake_pollable(struct pollable *pollable);
int poll_wait(struct poll *poll, struct poll_event *event, int timeout);
void poll_destroy(struct poll *poll);

// real fs
extern const struct fs_ops realfs;
extern const struct fd_ops realfs_fdops; // TODO remove from header file

// TODO put this somewhere else
char *strnprepend(char *str, const char *prefix, size_t max);

#endif
