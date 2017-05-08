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
};

void handle_interrupt(struct cpu_state *cpu, int interrupt) {
    if (interrupt == INT_SYSCALL) {
        int syscall_num = cpu->eax;
        int result;
        if (syscall_num >= NUM_SYSCALLS || syscall_table[syscall_num] == NULL) {
            result = _ENOSYS;
        } else {
            result = syscall_table[syscall_num](cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi, cpu->ebp);
        }
        cpu->eax = result;
    } else {
        printf("interrupt %d, exiting\n", interrupt);
        sys_exit(interrupt);
    }
}

int user_get_string(addr_t addr, char *buf, size_t max) {
    size_t i = 0;
    while (i < max) {
        buf[i] = MEM_GET(&current->cpu, addr + i, 8);
        if (buf[i] == '\0') break;
        i++;
    }
    return i;
}

int user_get_count(addr_t addr, char *buf, size_t count) {
    size_t i = 0;
    while (i < count) {
        buf[i] = MEM_GET(&current->cpu, addr + i, 8);
        i++;
    }
    return i;
}
