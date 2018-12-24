#include <string.h>
#include "kernel/errno.h"
#include "kernel/random.h"
#include "fs/mem.h"
#include "fs/dev.h"

// this file handles major device number 1, minor device numbers are mapped in table below
#pragma GCC diagnostic ignored "-Winitializer-overrides"
struct dev_ops *mem_devs[] = {
    [0 ... 255] = NULL,
    // [1] = &prog_mem_dev,
    // [2] = &kmem_dev, // (not really applicable)
    [3] = &null_dev,
    // [4] = &port_dev,
    [5] = &zero_dev,
    [7] = &full_dev,
    [8] = &random_dev,
    [9] = &random_dev,
    // [10] = &aio_dev,
    // [11] = &kmsg_dev,
    // [12] = &oldmem_dev, // replaced by /proc/vmcore
};

// dispatch device for major device 1
static int mem_open(int major, int minor, int type, struct fd *fd) {
    struct dev_ops *dev = mem_devs[minor];
    if (dev == NULL) {
        return _ENXIO;
    }
    fd->ops = &dev->fd;
    if (!dev->open)
        return 0;
    return dev->open(major, minor, type, fd);
}

struct dev_ops mem_dev = {
    .open = mem_open,
};

// begin inline devices
static int null_open(int major, int minor, int type, struct fd *fd) {
    return 0;
}
static ssize_t null_read(struct fd *fd, void *buf, size_t bufsize) {
    return 0;
}
static ssize_t null_write(struct fd *fd, const void *buf, size_t bufsize) {
    return bufsize;
}
struct dev_ops null_dev = {
    .open = null_open,
    .fd.read = null_read,
    .fd.write = null_write,
};

static ssize_t zero_read(struct fd *fd, void *buf, size_t bufsize) {
    memset(buf, 0, bufsize);
    return bufsize;
}
static ssize_t zero_write(struct fd *fd, const void *buf, size_t bufsize) {
    return bufsize;
}
struct dev_ops zero_dev = {
    .open = null_open,
    .fd.read = zero_read,
    .fd.write = zero_write,
};

static ssize_t full_write(struct fd *fd, const void *buf, size_t bufsize) {
    return _ENOSPC;
}
struct dev_ops full_dev = {
    .open = null_open,
    .fd.read = zero_read,
    .fd.write = full_write,
};

static ssize_t random_read(struct fd *fd, void *buf, size_t bufsize) {
    get_random(buf, bufsize);
    return bufsize;
}
struct dev_ops random_dev = {
    .open = null_open,
    .fd.read = random_read,
    .fd.write = null_write,
};
