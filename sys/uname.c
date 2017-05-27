#include <string.h>
#include "sys/calls.h"

int sys_uname(struct uname *uts) {
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
