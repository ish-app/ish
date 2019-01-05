#include <sys/utsname.h>
#include <string.h>
#if __APPLE__
#include <sys/sysctl.h>
#elif __linux__
#include <sys/sysinfo.h>
#endif
#include "kernel/calls.h"

void do_uname(struct uname *uts) {
    struct utsname real_uname;
    uname(&real_uname);

    memset(uts, 0, sizeof(struct uname));
    strcpy(uts->system, "Linux");
    strcpy(uts->hostname, real_uname.nodename);
    strcpy(uts->release, "3.2.0-ish");
    strcpy(uts->version, "SUPER AWESOME compiled on " __DATE__ );
    strcpy(uts->arch, "i686");
    strcpy(uts->domain, "(none)");
}

dword_t sys_uname(addr_t uts_addr) {
    struct uname uts;
    do_uname(&uts);
    if (user_put(uts_addr, uts))
        return _EFAULT;
    return 0;
}

dword_t sys_sethostname(addr_t UNUSED(hostname_addr), dword_t UNUSED(hostname_len)) {
    return _EPERM;
}

#if __APPLE__
static uint64_t get_total_ram() {
    uint64_t total_ram;
    sysctl((int []) {CTL_DEBUG, HW_PHYSMEM}, 2, &total_ram, NULL, NULL, 0);
    return total_ram;
}
static uint64_t get_uptime() {
    uint64_t value[2];
    size_t size = sizeof(value);
    sysctlbyname("kern.boottime", &value, &size, NULL, 0);
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec - value[0];
}
static void sysinfo_specific(struct sys_info *info) {
    info->totalram = get_total_ram();
    info->uptime = get_uptime();
    // TODO: everything else
}
#elif __linux__
static void sysinfo_specific(struct sys_info *info) {
    struct sysinfo host_info;
    sysinfo(&host_info);
    memcpy(info->loads, host_info.loads, sizeof(host_info.loads));
    info->uptime = host_info.uptime;
    info->totalram = host_info.totalram;
    info->freeram = host_info.freeram;
    info->sharedram = host_info.sharedram;
    info->totalswap = host_info.totalswap;
    info->freeswap = host_info.freeswap;
    info->procs = host_info.procs;
    info->totalhigh = host_info.totalhigh;
    info->freehigh = host_info.freehigh;
    info->mem_unit = host_info.mem_unit;
}
#endif

dword_t sys_sysinfo(addr_t info_addr) {
    struct sys_info info = {0};
    sysinfo_specific(&info);

    if (user_put(info_addr, info))
        return _EFAULT;
    return 0;
}
