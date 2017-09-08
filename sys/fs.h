#ifndef FS_H
#define FS_H

#include "misc.h"
#include "util/list.h"
#include "fs/stat.h"
#include <dirent.h>

#define MAX_PATH 4096
#define MAX_NAME 255

struct poll;

struct fd {
    unsigned refcnt;
    unsigned flags;
    const struct fd_ops *ops;
    const struct fs_ops *fs;

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
struct fd *fd_create();
typedef sdword_t fd_t;
#define MAX_FD 1024 // dynamically expanding fd table coming soon:tm:
#define AT_FDCWD_ ((fd_t) -100)

struct fd *generic_lookup(struct fd *dir, const char *name, int flags);
struct fd *generic_open(const char *path, int flags, int mode);
struct fd *generic_openat(struct fd *dir, const char *pathname, int flags, int mode);
struct fd *generic_dup(struct fd *fd);
int generic_close(struct fd *fd);
int generic_unlink(const char *pathname);
#define AC_R 4
#define AC_W 2
#define AC_X 1
#define AC_F 0
int generic_access(const char *pathname, int mode);
int generic_stat(const char *path, struct statbuf *stat, bool follow_links);
int generic_fstat(struct fd *fd, struct statbuf *stat);
ssize_t generic_readlink(const char *pathname, char *buf, size_t bufsize);

// currently there is only one mount exists
struct mount {
    const char *source;
    struct fd *root;
    const struct fs_ops *fs;
    struct mount *next;
};
struct mount *mounts;

struct dir_entry {
    qword_t inode;
    qword_t offset;
    char name[MAX_NAME + 1];
};

// open flags
#define O_RDONLY_ 0
#define O_CREAT_ (1 << 6)
#define O_DIRECTORY_ (1 << 16)

struct statfs_ {
    dword_t type;
    dword_t bsize;
    sdword_t blocks;
    sdword_t bfree;
    sdword_t bavail;
    dword_t files;
    dword_t ffree;
    dword_t namelen;
    dword_t frsize;
    dword_t flags;
    dword_t pad[4];
};

struct fs_ops {
    // will be called once, on mount
    struct fd *(*open_root)(struct mount *mount);

    // Opens the file with the given name in the directory
    struct fd *(*lookup)(struct fd *dir, const char *name, int flags);
    // Reads a directory entry from the stream
    // (this makes no sense, is therefore subject to change)
    int (*readdir)(struct fd *dir, struct dir_entry *entry);
    int (*unlink)(struct fd *dir, const char *name);
    int (*access)(struct fd *dir, const char *name, int mode);
    ssize_t (*readlink)(struct fd *dir, const char *name, char *buf, size_t bufsize);
    int (*stat)(struct fd *dir, const char *name, struct statbuf *stat, bool follow_links);
    int (*fstat)(struct fd *fd, struct statbuf *stat);

    int (*statfs)(struct statfs_ *buf);

    int (*flock)(struct fd *fd, int operation);
};

struct fd_ops {
    ssize_t (*read)(struct fd *fd, void *buf, size_t bufsize);
    ssize_t (*write)(struct fd *fd, const void *buf, size_t bufsize);
    off_t (*lseek)(struct fd *fd, off_t off, int whence);

    // memory returned must be allocated with mmap, as it is freed with munmap
    int (*mmap)(struct fd *fd, off_t offset, size_t len, int prot, int flags, void **mem_out);

    // returns a bitmask of operations that won't block
    int (*poll)(struct fd *fd);

    // returns the size needed for the output of ioctl, 0 if the arg is not a
    // pointer, -1 for invalid command
    ssize_t (*ioctl_size)(struct fd *fd, int cmd);
    // if ioctl_size returns non-zero, arg must point to ioctl_size valid bytes
    int (*ioctl)(struct fd *fd, int cmd, void *arg);

    // Returns the path of the file descriptor, buf must be at least MAX_PATH
    int (*getpath)(struct fd *fd, char *buf);

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

#endif
