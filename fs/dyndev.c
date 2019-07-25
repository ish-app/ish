#include "kernel/errno.h"
#include "fs/dev.h"
#include "fs/dyndev.h"

// Handles DYNDEV_MAJOR device number
// XXX: unregister might be added later
struct dyn_dev_info {
    // devs & next_dev lock
    lock_t devs_lock;
    // table of dev_ops registered by minor number
    struct dev_ops* devs[256];
    // next free devs slot
    // it's easier to detect overflow with u16 than with u8
    uint16_t next_dev;
} dyn_info;

int dyn_dev_register(struct dev_ops *ops) {
    lock(&dyn_info.devs_lock);

    int minor = dyn_info.next_dev;
    // Only up to 256 devices can be registered
    if (minor == 256) {
        unlock(&dyn_info.devs_lock);
        return _ENOSPC;
    }

    ++dyn_info.next_dev;

    assert(dyn_info.devs[minor] == NULL);
    dyn_info.devs[minor] = ops;

    unlock(&dyn_info.devs_lock);
    return minor;
}

static int dyn_open(int major, int minor, struct fd *fd) {
    // it's safe to access devs without locking (read-only)
    struct dev_ops *ops = dyn_info.devs[minor];
    if (ops == NULL) {
        return _ENXIO;
    }
    fd->ops = &ops->fd;

    // Succeed if there's no open provided by ops
    if (!ops->open)
        return 0;
    return ops->open(major, minor, fd);
}
struct dev_ops dyn_dev = {
    .open = dyn_open,
};
