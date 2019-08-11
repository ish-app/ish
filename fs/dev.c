#include "kernel/errno.h"
#include "fs/fd.h"
#include "fs/dev.h"
#include "fs/mem.h"
#include "fs/tty.h"
#include "fs/dyndev.h"
#include "fs/devices.h"

struct dev_ops *block_devs[256] = {
    // no block devices yet
};
struct dev_ops *char_devs[256] = {
    [MEM_MAJOR] = &mem_dev,
    [TTY_CONSOLE_MAJOR] = &tty_dev,
    [TTY_ALTERNATE_MAJOR] = &tty_dev,
    [TTY_PSEUDO_MASTER_MAJOR] = &tty_dev,
    [TTY_PSEUDO_SLAVE_MAJOR] = &tty_dev,
    [DYN_DEV_MAJOR] = &dyn_dev_char,
};

int dev_open(int major, int minor, int type, struct fd *fd) {
    struct dev_ops *dev = (type == DEV_BLOCK ? block_devs : char_devs)[major];
    if (dev == NULL)
        return _ENXIO;
    fd->ops = &dev->fd;
    if (!dev->open)
        return 0;
    return dev->open(major, minor, fd);
}
