#ifndef FS_H
#define FS_H

#include "misc.h"
#include "util/list.h"
#include "fs/stat.h"
#include "emu/memory.h"
#include <dirent.h>
#include <gdbm.h>

#define MAX_PATH 4096
#define MAX_NAME 256

struct poll;

struct fd {
    atomic_uint refcount;
    unsigned flags;
    const struct fd_ops *ops;
    struct list poll_fds;

    // fd data
    union {
        struct {
            DIR *dir;
        };
        struct {
            struct tty *tty;
            // links together fds pointing to the same tty
            // locked by the tty
            struct list other_fds;
        };
    };

    // fs/inode data
    struct mount *mount;
    union {
        int real_fd;
        struct statbuf *stat;
    };

    lock_t lock;
};

typedef sdword_t fd_t;
#define FD_CLOEXEC_ 1
#define AT_FDCWD_ -100

struct fd *fd_create(void);
struct fd *fd_dup(struct fd *fd);

struct attr {
    enum attr_type {
        attr_uid,
        attr_gid,
        attr_mode,
        attr_size,
    } type;
    union {
        uid_t_ uid;
        uid_t_ gid;
        mode_t_ mode;
        off_t_ size;
    };
};
#define make_attr(_type, thing) \
    ((struct attr) {.type = attr_##_type, ._type = thing})

#define AT_SYMLINK_NOFOLLOW_ 0x100

struct fd *generic_open(const char *path, int flags, int mode);
struct fd *generic_openat(struct fd *at, const char *path, int flags, int mode);
int fd_close(struct fd *fd);
int generic_linkat(struct fd *src_at, const char *src_raw, struct fd *dst_at, const char *dst_raw);
int generic_unlinkat(struct fd *at, const char *path);
int generic_rmdirat(struct fd *at, const char *path);
int generic_renameat(struct fd *src_at, const char *src, struct fd *dst_at, const char *dst);
int generic_symlinkat(const char *target, struct fd *at, const char *link);
#define AC_R 4
#define AC_W 2
#define AC_X 1
#define AC_F 0
int generic_access(const char *path, int mode);
int generic_statat(struct fd *at, const char *path, struct statbuf *stat, bool follow_links);
int generic_setattrat(struct fd *at, const char *path, struct attr attr, bool follow_links);
ssize_t generic_readlink(const char *path, char *buf, size_t bufsize);
int generic_mkdirat(struct fd *at, const char *path, mode_t_ mode);

// Converts an at argument to a system call to a struct fd *, returns NULL if you pass a bad fd
struct fd *at_fd(fd_t fd);

struct mount {
    const char *point;
    const char *source;
    const struct fs_ops *fs;
    struct mount *next;

    int root_fd;
    union {
        void *data;
        GDBM_FILE db;
    };
};
extern struct mount *mounts;

// open flags
#define O_RDONLY_ 0
#define O_WRONLY_ (1 << 0)
#define O_RDWR_ (1 << 1)
#define O_CREAT_ (1 << 6)

struct fs_ops {
    int (*mount)(struct mount *mount);
    int (*umount)(struct mount *mount);
    int (*statfs)(struct mount *mount, struct statfsbuf *stat);

    struct fd *(*open)(struct mount *mount, const char *path, int flags, int mode);
    ssize_t (*readlink)(struct mount *mount, const char *path, char *buf, size_t bufsize);
    int (*link)(struct mount *mount, const char *src, const char *dst);
    int (*unlink)(struct mount *mount, const char *path);
    int (*rmdir)(struct mount *mount, const char *path);
    int (*rename)(struct mount *mount, const char *src, const char *dst);
    int (*symlink)(struct mount *mount, const char *target, const char *link);

    int (*stat)(struct mount *mount, const char *path, struct statbuf *stat, bool follow_links);
    int (*fstat)(struct fd *fd, struct statbuf *stat);
    int (*setattr)(struct mount *mount, const char *path, struct attr attr);
    int (*fsetattr)(struct fd *fd, struct attr attr);
    int (*utime)(struct mount *mount, const char *path, struct timespec atime, struct timespec mtime);

    int (*mkdir)(struct mount *mount, const char *path, mode_t_ mode);

    int (*flock)(struct fd *fd, int operation);
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
    off_t_ (*lseek)(struct fd *fd, off_t_ off, int whence);

    // Reads a directory entry from the stream
    int (*readdir)(struct fd *fd, struct dir_entry *entry);

    // map the file
    int (*mmap)(struct fd *fd, struct mem *mem, page_t start, pages_t pages, off_t offset, int prot, int flags);

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

struct mount *find_mount(char *path);
struct mount *find_mount_and_trim_path(char *path);
const char *fix_path(const char *path); // TODO reconsider

// Normalizes the path specified and writes the result into the out buffer.
//
// Normalization means:
//  - prepending the current or root directory
//  - converting multiple slashes into one
//  - resolving . and ..
//  - resolving symlinks, skipping the last path component if the follow_links
//    argument is true
// The result will always begin with a slash or be empty.
//
// If the normalized path plus the null terminator would be longer than
// MAX_PATH, _ENAMETOOLONG is returned. The out buffer is expected to be at
// least MAX_PATH in size.
int path_normalize(struct fd *at, const char *path, char *out, bool follow_links);
bool path_is_normalized(const char *path);

// real fs
extern const struct fs_ops realfs;

int realfs_stat(struct mount *mount, const char *path, struct statbuf *fake_stat, bool follow_links);
int realfs_fstat(struct fd *fd, struct statbuf *fake_stat);
int realfs_statfs(struct mount *mount, struct statfsbuf *stat);
int realfs_flock(struct fd *fd, int operation);
extern const struct fd_ops realfs_fdops;
ssize_t realfs_read(struct fd *fd, void *buf, size_t bufsize);
ssize_t realfs_write(struct fd *fd, const void *buf, size_t bufsize);
int realfs_close(struct fd *fd);

// adhoc fs
struct fd *adhoc_fd_create(void);

// fake fs
extern const struct fs_ops fakefs;

#endif
