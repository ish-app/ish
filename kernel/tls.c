#include "kernel/calls.h"

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

int task_set_thread_area(struct task *task, addr_t u_info) {
    struct user_desc info;
    if (user_get_task(task, u_info, info))
        return _EFAULT;

    // On a real system, TLS works by creating a special segment pointing to
    // the TLS buffer. Our shitty emulation of that is to ignore attempts to
    // modify GS and add this address to any memory reference that uses GS.
    task->cpu.tls_ptr = info.base_addr;

    if (info.entry_number == (unsigned) -1) {
        info.entry_number = 0xc;
    }

    if (user_put(u_info, info))
            return _EFAULT;
    return 0;
}

int sys_set_thread_area(addr_t u_info) {
    STRACE("set_thread_area(0x%x)", u_info);
    return task_set_thread_area(current, u_info);
}

int sys_set_tid_address(addr_t tid) {
    current->clear_tid = tid;
    return sys_getpid();
}
