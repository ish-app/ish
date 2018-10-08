#ifndef FS_H
#define FS_H

#include "misc.h"
#include "util/list.h"
#include "fs/stat.h"
#include "emu/memory.h"
#include <dirent.h>
#include <gdbm.h>

struct fs_info {
    atomic_uint refcount;
    mode_t_ umask;
    struct fd *pwd;
    struct fd *root;
    lock_t lock;
};
struct fs_info *fs_info_new();
struct fs_info *fs_info_copy(struct fs_info *fs);
void fs_info_release(struct fs_info *fs);

void fs_chdir(struct fs_info *fs, struct fd *pwd);

#define MAX_PATH 4096
#define MAX_NAME 256

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
int generic_utime(struct fd *at, const char *path, struct timespec atime, struct timespec mtime, bool follow_links);
ssize_t generic_readlinkat(struct fd *at, const char *path, char *buf, size_t bufsize);
int generic_mkdirat(struct fd *at, const char *path, mode_t_ mode);

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
    // Returns the path of the file descriptor, buf must be at least MAX_PATH
    int (*getpath)(struct fd *fd, char *buf);

    int (*mkdir)(struct mount *mount, const char *path, mode_t_ mode);

    int (*flock)(struct fd *fd, int operation);
};

struct mount *find_mount(char *path);
struct mount *find_mount_and_trim_path(char *path);
const char *fix_path(const char *path); // TODO reconsider

// real fs
extern const struct fs_ops realfs;
extern const struct fs_ops fakefs;
extern const struct fd_ops realfs_fdops;

int realfs_truncate(struct mount *mount, const char *path, off_t_ size);
int realfs_utime(struct mount *mount, const char *path, struct timespec atime, struct timespec mtime);

int realfs_statfs(struct mount *mount, struct statfsbuf *stat);
int realfs_flock(struct fd *fd, int operation);
int realfs_getpath(struct fd *fd, char *buf);
ssize_t realfs_read(struct fd *fd, void *buf, size_t bufsize);
ssize_t realfs_write(struct fd *fd, const void *buf, size_t bufsize);
int realfs_close(struct fd *fd);

// adhoc fs
struct fd *adhoc_fd_create(void);

#endif
