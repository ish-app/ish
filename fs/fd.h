#ifndef FD_H
#define FD_H
#include <dirent.h>
#include "emu/memory.h"
#include "util/list.h"
#include "util/sync.h"
#include "util/bits.h"
#include "fs/stat.h"
#include "fs/proc.h"
#include "fs/sockrestart.h"

// FIXME almost everything that uses the structs in this file does so without any kind of sane locking

struct fd {
    atomic_uint refcount;
    unsigned flags;
    mode_t_ type; // just the S_IFMT part, it can't change
    const struct fd_ops *ops;
    struct list poll_fds;
    lock_t poll_lock;
    unsigned long offset;

    // fd data
    union {
        // tty
        struct {
            struct tty *tty;
            // links together fds pointing to the same tty
            // locked by the tty
            struct list other_fds;
        };
        // epoll
        struct {
            struct poll *poll;
        };
        // eventfd
        struct {
            uint64_t eventfd_val;
        };
        // timerfd
        struct {
            struct timer *timer;
            uint64_t expirations;
        };
    };
    // fs data
    union {
        // proc
        struct {
            struct proc_entry proc_entry;
            unsigned proc_dir_index;
            char *proc_data;
            size_t proc_size;
        };
        // devpts
        struct {
            int pty_num;
        };
    };

    // fs/inode data
    struct mount *mount;
    int real_fd; // seeks on this fd require the lock
    DIR *dir;
    struct inode_data *inode;
    ino_t fake_inode;
    struct statbuf stat; // for adhoc fs
    struct fd_sockrestart sockrestart; // argh

    // these are used for a variety of things related to the fd
    lock_t lock;
    cond_t cond;
};

typedef sdword_t fd_t;
#define AT_FDCWD_ -100

struct fd *fd_create(const struct fd_ops *ops);
struct fd *fd_retain(struct fd *fd);
int fd_close(struct fd *fd);

int fd_getflags(struct fd *fd);
int fd_setflags(struct fd *fd, int flags);

#define NAME_MAX 255
struct dir_entry {
    qword_t inode;
    char name[NAME_MAX + 1];
};

#define LSEEK_SET 0
#define LSEEK_CUR 1
#define LSEEK_END 2

struct fd_ops {
    // required for files
    // TODO make optional for non-files
    ssize_t (*read)(struct fd *fd, void *buf, size_t bufsize);
    ssize_t (*write)(struct fd *fd, const void *buf, size_t bufsize);
    off_t_ (*lseek)(struct fd *fd, off_t_ off, int whence);

    // Reads a directory entry from the stream
    // required for directories
    int (*readdir)(struct fd *fd, struct dir_entry *entry);
    // Return an opaque value representing the current point in the directory stream
    // optional, fd->offset will be used instead
    unsigned long (*telldir)(struct fd *fd);
    // Seek to the location represented by a pointer returned from telldir
    // optional, fd->offset will be used instead
    void (*seekdir)(struct fd *fd, unsigned long ptr);

    // map the file
    int (*mmap)(struct fd *fd, struct mem *mem, page_t start, pages_t pages, off_t offset, int prot, int flags);

    // returns a bitmask of operations that won't block
    int (*poll)(struct fd *fd);

    // returns the size needed for the output of ioctl, 0 if the arg is not a
    // pointer, -1 for invalid command
    ssize_t (*ioctl_size)(int cmd);
    // if ioctl_size returns non-zero, arg must point to ioctl_size valid bytes
    int (*ioctl)(struct fd *fd, int cmd, void *arg);

    int (*fsync)(struct fd *fd);
    int (*close)(struct fd *fd);

    // handle F_GETFL, i.e. return open flags for this fd
    int (*getflags)(struct fd *fd);
    // handle F_SETFL, i.e. set O_NONBLOCK
    int (*setflags)(struct fd *fd, dword_t arg);
};

struct fdtable {
    atomic_uint refcount;
    int size;
    struct fd **files;
    bits_t *cloexec;
    lock_t lock;
};

struct fdtable *fdtable_new(int size);
void fdtable_release(struct fdtable *table);
struct fdtable *fdtable_copy(struct fdtable *table);
void fdtable_free(struct fdtable *table);
void fdtable_do_cloexec(struct fdtable *table);
struct fd *fdtable_get(struct fdtable *table, fd_t f);

struct fd *f_get(fd_t f);
// steals a reference to the fd, gives it to the table on success and destroys it on error
// flags is checked for O_CLOEXEC and O_NONBLOCK
fd_t f_install(struct fd *fd, int flags);
int f_close(fd_t f);

#endif
