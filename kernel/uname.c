#include <strings.h>
#include <string.h>
#if __APPLE__
#include <sys/sysctl.h>
#elif __linux__
#include <sys/sysinfo.h>
#endif
#include "kernel/calls.h"

int sys_uname(struct uname *uts) {
    bzero(uts, sizeof(struct uname));
    strcpy(uts->system, "Linux");
    strcpy(uts->hostname, "compotar");
    strcpy(uts->release, "3.2.0-ish");
    strcpy(uts->version, "SUPER AWESOME");
    strcpy(uts->arch, "i686");
    strcpy(uts->domain, "compotar.me");
    return 0;
}

dword_t _sys_uname(addr_t uts_addr) {
    struct uname uts;
    int res = sys_uname(&uts);
    if (user_put(uts_addr, uts))
        return _EFAULT;
    return res;
}

#if __APPLE__
static uint64_t get_total_ram() {
    uint64_t total_ram;
    sysctl((int []) {CTL_DEBUG, HW_PHYSMEM}, 2, &total_ram, NULL, NULL, 0);
    return total_ram;
}
#elif __linux__
static uint64_t get_total_ram() {
    struct sysinfo info;
    sysinfo(&info);
    return info.totalram;
}
#endif

dword_t sys_sysinfo(addr_t info_addr) {
    struct sys_info info;
    info.totalram = get_total_ram();
    // TODO everything else
    if (user_put(info_addr, info))
        return _EFAULT;
    return 0;
}
