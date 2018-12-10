#include "kernel/errno.h"
#include "fs/mem.h"
#include "fs/null.h"

// this file handles major device number 1, minor device numbers are mapped in table below
#pragma GCC diagnostic ignored "-Winitializer-overrides"
struct dev_ops *mem_devs[] = {
    [0 ... 255] = NULL,
    // [1] = &prog_mem_dev,
    // [2] = &kmem_dev, // (not really applicable)
    [3] = &null_dev,
    // [4] = &port_dev,
    // [5] = &zero_dev,
    // [7] = &full_dev,
    // [8] = &random_dev,
    // [9] = &random_dev,
    // [10] = &aio_dev,
    // [11] = &kmsg_dev,
    // [12] = &oldmem_dev, // replaced by /proc/vmcore
};   

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
