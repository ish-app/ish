#ifndef FS_H
#define FS_H

#include "misc.h"
#include "util/list.h"
#include "fs/stat.h"
#include "fs/dev.h"
#include "emu/memory.h"
#include <dirent.h>
#include <sqlite3.h>

struct fs_info {
    atomic_uint refcount;
    mode_t_ umask;
    struct fd *pwd;
    struct fd *root;
    lock_t lock;
};
struct fs_info *fs_info_new(void);
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
int generic_getpath(struct fd *fd, char *buf);
int generic_linkat(struct fd *src_at, const char *src_raw, struct fd *dst_at, const char *dst_raw);
int generic_unlinkat(struct fd *at, const char *path);
int generic_rmdirat(struct fd *at, const char *path);
int generic_renameat(struct fd *src_at, const char *src, struct fd *dst_at, const char *dst);
int generic_symlinkat(const char *target, struct fd *at, const char *link);
int generic_mknod(const char *path, mode_t_ mode, dev_t_ dev);
#define AC_R 4
#define AC_W 2
#define AC_X 1
#define AC_F 0
int generic_accessat(struct fd *dirfd, const char *path, int mode);
int generic_statat(struct fd *at, const char *path, struct statbuf *stat, bool follow_links);
int generic_setattrat(struct fd *at, const char *path, struct attr attr, bool follow_links);
int generic_utime(struct fd *at, const char *path, struct timespec atime, struct timespec mtime, bool follow_links);
ssize_t generic_readlinkat(struct fd *at, const char *path, char *buf, size_t bufsize);
int generic_mkdirat(struct fd *at, const char *path, mode_t_ mode);

int access_check(struct statbuf *stat, int check);

struct mount {
    const char *point;
    const char *source;
    int flags;
    const struct fs_ops *fs;
    unsigned refcount;
    struct list mounts;

    int root_fd;
    union {
        void *data;
        struct {
            sqlite3 *db;
            struct {
                sqlite3_stmt *begin;
                sqlite3_stmt *commit;
                sqlite3_stmt *rollback;
                sqlite3_stmt *path_get_inode;
                sqlite3_stmt *path_read_stat;
                sqlite3_stmt *path_create_stat;
                sqlite3_stmt *path_create_path;
                sqlite3_stmt *inode_read_stat;
                sqlite3_stmt *inode_write_stat;
                sqlite3_stmt *path_link;
                sqlite3_stmt *path_unlink;
                sqlite3_stmt *path_rename;
                sqlite3_stmt *path_from_inode;
                sqlite3_stmt *try_cleanup_inode;
            } stmt;
            lock_t lock;
        };
    };
};
extern lock_t mounts_lock;

// returns a reference, which must be released
struct mount *mount_find(char *path);
void mount_retain(struct mount *mount);
void mount_release(struct mount *mount);

// must hold mounts_lock while calling these, or traversing mounts
int do_mount(const struct fs_ops *fs, const char *source, const char *point, int flags);
int do_umount(const char *point);
int mount_remove(struct mount *mount);
extern struct list mounts;

// open flags
#define O_RDONLY_ 0
#define O_WRONLY_ (1 << 0)
#define O_RDWR_ (1 << 1)
#define O_CREAT_ (1 << 6)
#define O_NOCTTY_ (1 << 8)
#define O_TRUNC_ (1 << 9)
#define O_APPEND_ (1 << 10)
#define O_NONBLOCK_ (1 << 11)
#define O_DIRECTORY_ (1 << 16)
#define O_CLOEXEC_ (1 << 19)

// generic ioctls
#define FIONREAD_ 0x541b
#define FIONBIO_ 0x5421

struct fs_ops {
    const char *name;
    int magic;

    int (*mount)(struct mount *mount);
    int (*umount)(struct mount *mount);
    int (*statfs)(struct mount *mount, struct statfsbuf *stat);

    struct fd *(*open)(struct mount *mount, const char *path, int flags, int mode); // required
    ssize_t (*readlink)(struct mount *mount, const char *path, char *buf, size_t bufsize);
    // TODO make these optional (EROFS probably)
    int (*link)(struct mount *mount, const char *src, const char *dst);
    int (*unlink)(struct mount *mount, const char *path);
    int (*rmdir)(struct mount *mount, const char *path);
    int (*rename)(struct mount *mount, const char *src, const char *dst);
    int (*symlink)(struct mount *mount, const char *target, const char *link);
    int (*mknod)(struct mount *mount, const char *path, mode_t_ mode, dev_t_ dev);
    int (*mkdir)(struct mount *mount, const char *path, mode_t_ mode);

    // There's a close function in both the fs and fd to handle device files
    // where, for instance, there's a real_fd needed for getpath and also a tty
    // reference, and both need to be released when the fd is closed.
    // If they are the same function, it will only be called once.
    int (*close)(struct fd *fd);

    int (*stat)(struct mount *mount, const char *path, struct statbuf *stat, bool follow_links);
    int (*fstat)(struct fd *fd, struct statbuf *stat);
    int (*setattr)(struct mount *mount, const char *path, struct attr attr);
    int (*fsetattr)(struct fd *fd, struct attr attr);
    int (*utime)(struct mount *mount, const char *path, struct timespec atime, struct timespec mtime);
    // Returns the path of the file descriptor, null terminated, buf must be at least MAX_PATH+1
    int (*getpath)(struct fd *fd, char *buf);

    int (*flock)(struct fd *fd, int operation);

    // If present, called when all references to an inode_data for this
    // filesystem go away.
    void (*inode_orphaned)(struct mount *mount, struct inode_data *inode);
};

struct mount *find_mount_and_trim_path(char *path);
const char *fix_path(const char *path); // TODO reconsider

// real fs
extern const struct fd_ops realfs_fdops;

int realfs_truncate(struct mount *mount, const char *path, off_t_ size);
int realfs_utime(struct mount *mount, const char *path, struct timespec atime, struct timespec mtime);

int realfs_statfs(struct mount *mount, struct statfsbuf *stat);
int realfs_flock(struct fd *fd, int operation);
int realfs_getpath(struct fd *fd, char *buf);
ssize_t realfs_read(struct fd *fd, void *buf, size_t bufsize);
ssize_t realfs_write(struct fd *fd, const void *buf, size_t bufsize);
int realfs_poll(struct fd *fd);
int realfs_getflags(struct fd *fd);
int realfs_setflags(struct fd *fd, dword_t arg);
int realfs_close(struct fd *fd);

// adhoc fs
struct fd *adhoc_fd_create(const struct fd_ops *ops);

// filesystems
extern const struct fs_ops realfs;
extern const struct fs_ops procfs;
extern const struct fs_ops fakefs;
extern const struct fs_ops devptsfs;

#endif
