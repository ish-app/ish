#include <string.h>
#include "kernel/errno.h"
#include "kernel/random.h"
#include "fs/mem.h"
#include "fs/dev.h"

extern size_t iac_read(void *buf, size_t bufsize);
extern size_t iac_write(const void *buf, size_t bufsize);

// this file handles major device number 1, minor device numbers are mapped in table below
struct dev_ops *mem_devs[256] = {
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
    [99] = &iac_dev,

};

// dispatch device for major device 1
static int mem_open(int major, int minor, struct fd *fd) {
    struct dev_ops *dev = mem_devs[minor];
    if (dev == NULL) {
        return _ENXIO;
    }
    fd->ops = &dev->fd;
    if (!dev->open)
        return 0;
    return dev->open(major, minor, fd);
}

struct dev_ops mem_dev = {
    .open = mem_open,
};

// begin inline devices
static int iac_open(int UNUSED(major), int UNUSED(minor), struct fd *UNUSED(fd)) {
    return 0;
}
static ssize_t _iac_read(struct fd *UNUSED(fd), void *buf, size_t bufsize) {
    return iac_read(buf, bufsize); // Defined in IOSGateway.m
}
static ssize_t _iac_write(struct fd *UNUSED(fd), const void *buf, size_t bufsize) {
    return iac_write(buf, bufsize); // Defined in IOSGateway.m
}
struct dev_ops iac_dev = {
    .open = iac_open,
    .fd.read = _iac_read,
    .fd.write = _iac_write,
};

static int null_open(int UNUSED(major), int UNUSED(minor), struct fd *UNUSED(fd)) {
    return 0;
}
static ssize_t null_read(struct fd *UNUSED(fd), void *UNUSED(buf), size_t UNUSED(bufsize)) {
    return 0;
}
static ssize_t null_write(struct fd *UNUSED(fd), const void *UNUSED(buf), size_t bufsize) {
    return bufsize;
}
struct dev_ops null_dev = {
    .open = null_open,
    .fd.read = null_read,
    .fd.write = null_write,
};

static ssize_t zero_read(struct fd *UNUSED(fd), void *buf, size_t bufsize) {
    memset(buf, 0, bufsize);
    return bufsize;
}
static ssize_t zero_write(struct fd *UNUSED(fd), const void *UNUSED(buf), size_t bufsize) {
    return bufsize;
}
struct dev_ops zero_dev = {
    .open = null_open,
    .fd.read = zero_read,
    .fd.write = zero_write,
};

static ssize_t full_write(struct fd *UNUSED(fd), const void *UNUSED(buf), size_t UNUSED(bufsize)) {
    return _ENOSPC;
}
struct dev_ops full_dev = {
    .open = null_open,
    .fd.read = zero_read,
    .fd.write = full_write,
};

static ssize_t random_read(struct fd *UNUSED(fd), void *buf, size_t bufsize) {
    get_random(buf, bufsize);
    return bufsize;
}
struct dev_ops random_dev = {
    .open = null_open,
    .fd.read = random_read,
    .fd.write = null_write,
};
