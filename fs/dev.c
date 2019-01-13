#include "kernel/errno.h"
#include "fs/fd.h"
#include "fs/dev.h"
#include "fs/mem.h"
#include "fs/tty.h"

struct dev_ops *block_devs[256] = {
    // no block devices yet
};
struct dev_ops *char_devs[256] = {
    [1] = &mem_dev,
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
