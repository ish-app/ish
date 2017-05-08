#include <unistd.h>
#include "sys/calls.h"

ssize_t sys_write(int fd, const char *buf, size_t count) {
    // passthrough
    return write(fd, buf, count);
}

dword_t _sys_write(dword_t fd, addr_t data, dword_t count) {
    char buf[count];
    user_get_count(data, buf, count);
    return sys_write(fd, buf, count);
}
