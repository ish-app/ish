#ifndef DEV_H
#define DEV_H

#include <sys/types.h>
#if __linux__
#include <sys/sysmacros.h>
#endif
#include "fs/fd.h"

// a dev_t is encoded like this in hex, where M is major and m is minor:
// mmmMMMmm
// (legacy I guess)

typedef uint32_t dev_t_;

static inline dev_t_ dev_make(int major, int minor) {
    return ((minor & 0xfff00) << 12) | (major << 8) | (minor & 0xff);
}
static inline int dev_major(dev_t_ dev) {
    return (dev & 0xfff00) >> 8;
}
static inline int dev_minor(dev_t_ dev) {
    return ((dev & 0xfff00000) >> 12) | (dev & 0xff);
}

static inline dev_t dev_real_from_fake(dev_t_ dev) {
    return makedev(dev_major(dev), dev_minor(dev));
}
static inline dev_t_ dev_fake_from_real(dev_t dev) {
    return dev_make(major(dev), minor(dev));
}

#define DEV_BLOCK 0
#define DEV_CHAR 1

struct dev_ops {
    int (*open)(int major, int minor, struct fd *fd);
    struct fd_ops fd;
};

extern struct dev_ops *block_devs[];
extern struct dev_ops *char_devs[];

int dev_open(int major, int minor, int type, struct fd *fd);

extern struct dev_ops null_dev;

#endif
