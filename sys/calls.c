#include "emu/process.h"
#include "sys/calls.h"
#include "sys/errno.h"

#define NUM_SYSCALLS 300

#pragma GCC diagnostic ignored "-Winitializer-overrides"
syscall_t syscall_table[] = {
    [0 ... NUM_SYSCALLS] = NULL,

    [1] =  (syscall_t) sys_exit, // 1
    [4] =  (syscall_t) _sys_write, // 4
    [11] = (syscall_t) _sys_execve, // 11
    [45] = (syscall_t) sys_brk, // 45

    [243] = (syscall_t) sys_set_thread_area,
};

void handle_interrupt(struct cpu_state *cpu, int interrupt) {
    if (interrupt == INT_SYSCALL) {
        int syscall_num = cpu->eax;
        TRACE("system call number %d\n", syscall_num);
        int result;
        if (syscall_num >= NUM_SYSCALLS || syscall_table[syscall_num] == NULL) {
            // TODO SIGSYS
            result = _ENOSYS;
        } else {
            result = syscall_table[syscall_num](cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi, cpu->ebp);
        }
        TRACE("result: %x\n", result);
        cpu->eax = result;
    } else {
        printf("interrupt %d, exiting\n", interrupt);
        sys_exit(interrupt);
    }
}
