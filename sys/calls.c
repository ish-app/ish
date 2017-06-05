#include "emu/process.h"
#include "sys/calls.h"
#include "sys/errno.h"

#define NUM_SYSCALLS 300

#pragma GCC diagnostic ignored "-Winitializer-overrides"
syscall_t syscall_table[] = {
    [0 ... NUM_SYSCALLS] = NULL,

    [1] =  (syscall_t) sys_exit,
    [4] =  (syscall_t) _sys_write,
    [11] = (syscall_t) _sys_execve,
    [45] = (syscall_t) sys_brk,
    [85] = (syscall_t) sys_readlink,
    [122] = (syscall_t) _sys_uname,
    [243] = (syscall_t) sys_set_thread_area,
    [252] = (syscall_t) sys_exit_group,
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
            result = _ENOSYS;
        } else {
            result = syscall_table[syscall_num](cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi, cpu->ebp);
        }
        TRACE("result %x\n", result);
        cpu->eax = result;
    } else if (interrupt == INT_GPF) {
        // page fault handling is a thing
        return handle_pagefault(cpu->segfault_addr);
    } else {
        printf("exiting\n");
        sys_exit(interrupt);
    }
    return 0;
}
