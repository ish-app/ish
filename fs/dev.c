#include "sys/errno.h"
#include "fs/dev.h"

struct dev_ops *block_devs[] = {
    [0 ... 255] = NULL,
};
struct dev_ops *char_devs[] = {
    [0 ... 255] = NULL,
};

int dev_open(int major, int minor, int type, struct fd *fd) {
    struct dev_ops *dev = (type == DEV_BLOCK ? block_devs : char_devs)[major];
    if (dev == NULL)
        return _ENODEV;

    int err = dev->open(major, minor, type, fd);
    if (err < 0)
        return err;

    fd->ops = &dev->fd;
    return 0;
}

