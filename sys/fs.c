#include "sys/calls.h"
#include "sys/errno.h"
#include "fs/fs.h"

dword_t sys_readlink(addr_t pathname_addr, addr_t buf_addr, dword_t bufsize) {
    return _ENOENT;
}
