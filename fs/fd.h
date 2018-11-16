#ifndef FD_H
#define FD_H
#include <dirent.h>
#include "emu/memory.h"
#include "util/list.h"
#include "util/sync.h"
#include "util/bits.h"
#include "fs/stat.h"

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
    // seeks on this fd require the lock
    int real_fd;
    struct statbuf stat; // for adhoc fs

    lock_t lock;
};

typedef sdword_t fd_t;
#define FD_CLOEXEC_ 1
#define AT_FDCWD_ -100

struct fd *fd_create(void);
struct fd *fd_retain(struct fd *fd);
int fd_close(struct fd *fd);

#define NAME_MAX 255
struct dir_entry {
    qword_t inode;
    char name[NAME_MAX + 1];
};

struct fd_ops {
    ssize_t (*read)(struct fd *fd, void *buf, size_t bufsize);
    ssize_t (*write)(struct fd *fd, const void *buf, size_t bufsize);
    off_t_ (*lseek)(struct fd *fd, off_t_ off, int whence);

    // Reads a directory entry from the stream
    int (*readdir)(struct fd *fd, struct dir_entry *entry);
    // Return an opaque value representing the current point in the directory stream
    long (*telldir)(struct fd *fd);
    // Seek to the location represented by a pointer returned from telldir
    int (*seekdir)(struct fd *fd, long ptr);

    // map the file
    int (*mmap)(struct fd *fd, struct mem *mem, page_t start, pages_t pages, off_t offset, int prot, int flags);

    // returns a bitmask of operations that won't block
    int (*poll)(struct fd *fd);

    // returns the size needed for the output of ioctl, 0 if the arg is not a
    // pointer, -1 for invalid command
    ssize_t (*ioctl_size)(struct fd *fd, int cmd);
    // if ioctl_size returns non-zero, arg must point to ioctl_size valid bytes
    int (*ioctl)(struct fd *fd, int cmd, void *arg);

    int (*fsync)(struct fd *fd);
    int (*close)(struct fd *fd);

    // handle F_GETFL, i.e. return open flags for this fd
    int (*getflags)(struct fd *fd);
};

struct fdtable {
    atomic_uint refcount;
    unsigned size;
    struct fd **files;
    bits_t *cloexec;
};

struct fdtable *fdtable_new(unsigned size);
void fdtable_release(struct fdtable *table);
int fdtable_resize(struct fdtable *table, unsigned size);
struct fdtable *fdtable_copy(struct fdtable *table);
void fdtable_free(struct fdtable *table);

struct fd *f_get(fd_t f);
bool f_is_cloexec(fd_t f);
void f_set_cloexec(fd_t f);
void f_put(fd_t f, struct fd *fd);
// steals a reference to the fd, gives it to the table on success and destroys it on error
fd_t f_install(struct fd *fd);
int f_close(fd_t f);

#endif
