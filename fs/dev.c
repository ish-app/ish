#include "kernel/errno.h"
#include "fs/fd.h"
#include "fs/dev.h"
#include "fs/tty.h"

#pragma GCC diagnostic ignored "-Winitializer-overrides"
struct dev_ops *block_devs[] = {
    [0 ... 255] = NULL,
};
struct dev_ops *char_devs[] = {
    [0 ... 255] = NULL,
    [1] = &null_dev,
    [4] = &tty_dev,
    [5] = &tty_dev,
};

int dev_open(int major, int minor, int type, struct fd *fd) {
    struct dev_ops *dev = (type == DEV_BLOCK ? block_devs : char_devs)[major];
    if (dev == NULL)
        return _ENXIO;
    fd->ops = &dev->fd;
    if (!dev->open)
        return 0;
    return dev->open(major, minor, type, fd);
}

// this device seemed so simple it was hardly worth making a new file for it

static int null_open(int major, int minor, int type, struct fd *fd) {
    if (minor != 3)
        return _ENXIO;
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
