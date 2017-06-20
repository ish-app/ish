#include "sys/calls.h"

struct user_desc {
    dword_t entry_number;
    dword_t base_addr;
    dword_t limit;
    unsigned int seg_32bit:1;
    unsigned int contents:2;
    unsigned int read_exec_only:1;
    unsigned int limit_in_pages:1;
    unsigned int seg_not_present:1;
    unsigned int useable:1;
};

int sys_set_thread_area(addr_t u_info) {
    struct user_desc info;
    user_get_count(u_info, &info, sizeof(struct user_desc));

    // On a real system, TLS works by creating a special segment pointing to
    // the TLS buffer. Our shitty emulation of that is to ignore attempts to
    // modify GS and add this address to any memory reference that uses GS.
    current->cpu.tls_ptr = info.base_addr;

    if (info.entry_number == (unsigned) -1) {
        info.entry_number = 0xc;
    }

    user_put_count(u_info, (char *) &info, sizeof(struct user_desc));
    return 0;
}
