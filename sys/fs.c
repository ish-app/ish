#include "sys/calls.h"
#include "sys/errno.h"
#include "fs/fs.h"

int sys_open(const char *pathname, int flags) {
    // meh
    return _ENOSYS;
}

dword_t sys_readlink(addr_t pathname_addr, addr_t buf_addr, dword_t bufsize) {
    char pathname[MAX_PATH];
    user_get_string(pathname_addr, pathname, sizeof(pathname));
    char buf[bufsize];
    int err = generic_readlink(pathname, buf, bufsize);
    if (err >= 0)
        user_put_string(buf_addr, buf);
    return err;
}
