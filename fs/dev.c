#include "kernel/errno.h"
#include "fs/dev.h"
#include "fs/tty.h"

#pragma GCC diagnostic ignored "-Winitializer-overrides"
struct dev_ops *block_devs[] = {
    [0 ... 255] = NULL,
};
struct dev_ops *char_devs[] = {
    [0 ... 255] = NULL,
    [4] = &tty_dev,
};

int dev_open(int major, int minor, int type, struct fd *fd) {
    struct dev_ops *dev = (type == DEV_BLOCK ? block_devs : char_devs)[major];
    if (dev == NULL)
        return _ENODEV;
    fd->ops = &dev->fd;
    return dev->open(major, minor, type, fd);
}

