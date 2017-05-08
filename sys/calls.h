#include "emu/cpu.h"
#include "emu/interrupt.h"
#include "misc.h"

void handle_interrupt(struct cpu_state *cpu, int interrupt);

int user_get_string(addr_t addr, char *buf, size_t max);
int user_get_count(addr_t addr, char *buf, size_t count);

int sys_execve(const char *file, char *const argv[], char *const envp[]);
int _sys_execve(addr_t file, addr_t argv, addr_t envp);

int sys_exit(dword_t status);

ssize_t sys_write(int fd, const char *buf, size_t count);
dword_t _sys_write(dword_t fd, addr_t data, dword_t count);

typedef int (*syscall_t)(dword_t,dword_t,dword_t,dword_t,dword_t,dword_t);

#define NUM_SYSCALLS (sizeof(syscall_table)/sizeof(syscall_table[0]))
