#include "emu/process.h"
#include "sys/calls.h"
#include "sys/errno.h"

syscall_t syscall_table[] = {
    NULL,         // 0
    (syscall_t) sys_exit, // 1
    NULL,         // 2
    NULL,         // 3
    (syscall_t) _sys_write, // 4
    NULL,         // 5
    NULL,         // 6
    NULL,         // 7
    NULL,         // 8
    NULL,         // 9
    NULL,         // 10
    (syscall_t) _sys_execve, // 11
    NULL,         // 12
    NULL,         // 13
    NULL,         // 14
    NULL,         // 15
    NULL,         // 16
    NULL,         // 17
    NULL,         // 18
    NULL,         // 19
    NULL,         // 20
    NULL,         // 21
    NULL,         // 22
    NULL,         // 23
    NULL,         // 24
    NULL,         // 25
    NULL,         // 26
    NULL,         // 27
    NULL,         // 28
    NULL,         // 29
    NULL,         // 30
    NULL,         // 31
    NULL,         // 32
    NULL,         // 33
    NULL,         // 34
    NULL,         // 35
    NULL,         // 36
    NULL,         // 37
    NULL,         // 38
    NULL,         // 39
    NULL,         // 40
    NULL,         // 41
    NULL,         // 42
    NULL,         // 43
    NULL,         // 44
    (syscall_t) sys_brk, // 45
};

#define NUM_SYSCALLS (sizeof(syscall_table)/sizeof(syscall_table[0]))

void handle_interrupt(struct cpu_state *cpu, int interrupt) {
    if (interrupt == INT_SYSCALL) {
        int syscall_num = cpu->eax;
        TRACE("system call number %d\n", syscall_num);
        int result;
        if (syscall_num >= NUM_SYSCALLS || syscall_table[syscall_num] == NULL) {
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
