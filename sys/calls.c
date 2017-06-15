#include "emu/process.h"
#include "sys/calls.h"
#include "sys/errno.h"

#define NUM_SYSCALLS 300

dword_t syscall_stub() {
    return _ENOSYS;
}

#pragma GCC diagnostic ignored "-Winitializer-overrides"
syscall_t syscall_table[] = {
    [0 ... NUM_SYSCALLS] = NULL,

    [1] =  (syscall_t) sys_exit,
    [3] = (syscall_t) sys_read,
    [4] =  (syscall_t) sys_write,
    [5] = (syscall_t) sys_open,
    [6] = (syscall_t) sys_close,
    [11] = (syscall_t) _sys_execve,
    [33] = (syscall_t) sys_access,
    [41] = (syscall_t) sys_dup,
    [45] = (syscall_t) sys_brk,
    [85] = (syscall_t) sys_readlink,
    [90] = (syscall_t) sys_mmap,
    [91] = (syscall_t) sys_munmap,
    [122] = (syscall_t) _sys_uname,
    [125] = (syscall_t) sys_mprotect,
    [146] = (syscall_t) sys_writev,
    [192] = (syscall_t) sys_mmap2,
    [197] = (syscall_t) sys_fstat64,
    [243] = (syscall_t) sys_set_thread_area,
    [252] = (syscall_t) sys_exit_group,

    // stubs
    [221] = (syscall_t) syscall_stub, // fcntl64
};

// returns true if a step is necessary (subject to change)
int handle_interrupt(struct cpu_state *cpu, int interrupt) {
    TRACE("\nint %d ", interrupt);
    if (interrupt == INT_SYSCALL) {
        int syscall_num = cpu->eax;
        TRACE("syscall %d ", syscall_num);
        int result;
        if (syscall_num >= NUM_SYSCALLS || syscall_table[syscall_num] == NULL) {
            // TODO SIGSYS
            TRACE("\n");
            debugger;
            exit(1);
        } else {
            result = syscall_table[syscall_num](cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi, cpu->ebp);
        }
        TRACE("result %x\n", result);
        cpu->eax = result;
    } else if (interrupt == INT_GPF) {
        // page fault handling is a thing
        TRACE("page fault at %x\n", cpu->segfault_addr);
        int res = handle_pagefault(cpu->segfault_addr);
        if (res == 0) {
            printf("could not handle page fault at %x, exiting\n", cpu->segfault_addr);
            sys_exit(1);
        }
        return res;
    } else {
        printf("exiting\n");
        sys_exit(interrupt);
    }
    return 0;
}
