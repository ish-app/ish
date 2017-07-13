#include "sys/calls.h"
#include "emu/interrupt.h"

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
    [10] = (syscall_t) sys_unlink,
    [11] = (syscall_t) _sys_execve,
    [13] = (syscall_t) sys_time,
    [19] = (syscall_t) sys_lseek,
    [20] = (syscall_t) sys_getpid,
    [24] = (syscall_t) sys_getuid,
    [47] = (syscall_t) sys_getgid,
    [33] = (syscall_t) sys_access,
    [41] = (syscall_t) sys_dup,
    [45] = (syscall_t) sys_brk,
    [49] = (syscall_t) sys_geteuid,
    [54] = (syscall_t) sys_ioctl,
    [63] = (syscall_t) sys_dup2,
    [64] = (syscall_t) sys_getppid,
    [85] = (syscall_t) sys_readlink,
    [90] = (syscall_t) sys_mmap,
    [91] = (syscall_t) sys_munmap,
    [116] = (syscall_t) sys_sysinfo,
    [120] = (syscall_t) sys_clone,
    [122] = (syscall_t) _sys_uname,
    [125] = (syscall_t) sys_mprotect,
    [140] = (syscall_t) sys__llseek,
    [146] = (syscall_t) sys_writev,
    [183] = (syscall_t) sys_getcwd,
    [174] = (syscall_t) sys_rt_sigaction,
    [187] = (syscall_t) sys_sendfile,
    [192] = (syscall_t) sys_mmap2,
    [195] = (syscall_t) sys_stat64,
    [196] = (syscall_t) sys_lstat64,
    [197] = (syscall_t) sys_fstat64,
    [199] = (syscall_t) sys_getuid32,
    /* [200] = (syscall_t) sys_getgid32, */
    [201] = (syscall_t) sys_geteuid32,
    [220] = (syscall_t) sys_getdents64,
    [239] = (syscall_t) sys_sendfile64,
    [243] = (syscall_t) sys_set_thread_area,
    [252] = (syscall_t) sys_exit_group,
    [265] = (syscall_t) sys_clock_gettime,

    // stubs
    [221] = (syscall_t) syscall_stub, // fcntl64
};

// returns true if a step is necessary (subject to change)
void handle_interrupt(struct cpu_state *cpu, int interrupt) {
    TRACE("\nint %d ", interrupt);
    if (interrupt == INT_SYSCALL) {
        int syscall_num = cpu->eax;
        if (syscall_num >= NUM_SYSCALLS || syscall_table[syscall_num] == NULL) {
            // TODO SIGSYS
            printf("missing syscall %d\n", syscall_num);
            if (send_signal(SIGSYS_) < 0)
                printf("send sigsys failed\n");
        } else {
            TRACE("syscall %d ", syscall_num);
            int result = syscall_table[syscall_num](cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi, cpu->ebp);
            TRACE("result %x\n", result);
            cpu->eax = result;
        }
    } else if (interrupt == INT_GPF) {
        // page fault handling is a thing
        // TODO SIGSEGV
        printf("could not handle page fault at %x, exiting\n", cpu->segfault_addr);
        sys_exit(1);
    } else if (interrupt == INT_UNDEFINED) {
        printf("illegal instruction\n");
        if (send_signal(SIGILL_) < 0)
            printf("send sigill failed\n");
    } else {
        printf("exiting\n");
        sys_exit(interrupt);
    }
}
