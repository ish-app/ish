#include "kernel/errno.h"
#include "fs/fd.h"
#include "fs/dev.h"
#include "fs/mem.h"
#include "fs/tty.h"

#pragma GCC diagnostic ignored "-Winitializer-overrides"
struct dev_ops *block_devs[] = {
    [0 ... 255] = NULL,
};
struct dev_ops *char_devs[] = {
    [0 ... 255] = NULL,
    [1] = &mem_dev,
    [4] = &tty_dev,
    [5] = &tty_dev,
};

int dev_open(int major, int minor, int type, struct fd *fd) {
    struct dev_ops *dev = (type == DEV_BLOCK ? block_devs : char_devs)[major];
    if (dev == NULL)
        return _ENXIO;
    if (dev->open) {
        int err = dev->open(major, minor, type, fd);
        if (err < 0)
            return err;
    }
    fd->ops = &dev->fd;
    return 0;
}
