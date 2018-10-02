#include <fcntl.h>
#include "kernel/calls.h"

dword_t sys_getrandom(addr_t buf_addr, dword_t len, dword_t flags) {
    if (len > 256)
        return _EIO;
    char buf[len];
    int dev_random = open("/dev/urandom", O_RDONLY);
    if (dev_random < 0)
        return _EIO;
    int err = read(dev_random, buf, len);
    close(dev_random);
    if (err < 0)
        return _EIO;
    if (user_put(buf_addr, buf))
        return _EFAULT;
    return len;
}
