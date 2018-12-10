#include <fcntl.h>
#include "kernel/calls.h"

#ifdef __APPLE__
#include <Security/Security.h>
#else
#include <linux/random.h>
#endif

int get_random(char *buf, size_t len) {
#ifdef __APPLE__
    return SecRandomCopyBytes(kSecRandomDefault, len, buf) != errSecSuccess;
#else
    return getrandom(buf, len, 0) < 0;
#endif
}

dword_t sys_getrandom(addr_t buf_addr, dword_t len, dword_t flags) {
    if (len > 256)
        return _EIO;
    char buf[256];
    if (get_random(buf, len) != 0)
        return _EIO;
    if (user_write(buf_addr, buf, len))
        return _EFAULT;
    return len;
}
