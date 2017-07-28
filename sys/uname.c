#include <strings.h>
#include <string.h>
#include <sys/sysinfo.h>
#include "sys/calls.h"

int sys_uname(struct uname *uts) {
    bzero(uts, sizeof(struct uname));
    strcpy(uts->system, "Linux");
    strcpy(uts->hostname, "compotar");
    strcpy(uts->release, "2.6.32-ish");
    strcpy(uts->version, "SUPER AWESOME");
    strcpy(uts->arch, "i686");
    strcpy(uts->domain, "compotar.me");
    return 0;
}

dword_t _sys_uname(addr_t uts_addr) {
    struct uname uts;
    int res = sys_uname(&uts);
    user_put_count(uts_addr, &uts, sizeof(struct uname));
    return res;
}

// TODO portability
dword_t sys_sysinfo(addr_t info_addr) {
    struct sys_info info;
    struct sysinfo real_info;
    sysinfo(&real_info);
    info.uptime = real_info.uptime;
    info.loads[0] = real_info.loads[0];
    info.loads[1] = real_info.loads[1];
    info.loads[2] = real_info.loads[2];
    info.totalram = real_info.totalram;
    info.freeram = real_info.freeram;
    info.sharedram = real_info.sharedram;
    info.bufferram = real_info.bufferram;
    info.totalswap = real_info.totalswap;
    info.freeswap = real_info.freeswap;
    info.procs = real_info.procs;
    info.totalhigh = real_info.totalhigh;
    info.freehigh = real_info.freehigh;
    info.mem_unit = real_info.mem_unit;
    user_put_count(info_addr, &info, sizeof(info));
    return 0;
}
