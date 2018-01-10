#include "kernel/calls.h"
#include "kernel/rlimit.h"

dword_t sys_getrlimit(dword_t resource, addr_t rlim_addr) {
    struct rlimit_ rlimit = current->limits[resource];
    if (user_put(rlim_addr, rlimit))
        return _EFAULT;
    return 0;
}

dword_t sys_setrlimit(dword_t resource, addr_t rlim_addr) {
    struct rlimit_ rlimit;
    if (user_get(rlim_addr, rlimit))
        return _EFAULT;
    // TODO check permissions
    current->limits[resource] = rlimit;
    return 0;
}

dword_t sys_prlimit(pid_t_ pid, dword_t resource, addr_t new_limit_addr, addr_t old_limit_addr) {
    if (pid != 0)
        return _EINVAL;

    int err = 0;
    if (old_limit_addr != 0) {
        err = sys_getrlimit(resource, old_limit_addr);
        if (err < 0)
            return err;
    }
    if (new_limit_addr != 0) {
        err = sys_setrlimit(resource, new_limit_addr);
        if (err < 0)
            return err;
    }
    return 0;
}
