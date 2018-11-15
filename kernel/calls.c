#include "debug.h"
#include "kernel/calls.h"
#include "emu/interrupt.h"

#define NUM_SYSCALLS 400

dword_t syscall_stub() {
    return _ENOSYS;
}

#pragma GCC diagnostic ignored "-Winitializer-overrides"
syscall_t syscall_table[] = {
    [0 ... NUM_SYSCALLS] = NULL,

    [1]   = (syscall_t) sys_exit,
    [2]   = (syscall_t) sys_fork,
    [3]   = (syscall_t) sys_read,
    [4]   = (syscall_t) sys_write,
    [5]   = (syscall_t) sys_open,
    [6]   = (syscall_t) sys_close,
    [7]   = (syscall_t) sys_waitpid,
    [9]   = (syscall_t) sys_link,
    [10]  = (syscall_t) sys_unlink,
    [11]  = (syscall_t) _sys_execve,
    [12]  = (syscall_t) sys_chdir,
    [13]  = (syscall_t) sys_time,
    [15]  = (syscall_t) sys_chmod,
    [19]  = (syscall_t) sys_lseek,
    [20]  = (syscall_t) sys_getpid,
    [21]  = (syscall_t) sys_mount,
    [24]  = (syscall_t) sys_getuid,
    [33]  = (syscall_t) sys_access,
    [37]  = (syscall_t) sys_kill,
    [38]  = (syscall_t) sys_rename,
    [39]  = (syscall_t) sys_mkdir,
    [40]  = (syscall_t) sys_rmdir,
    [41]  = (syscall_t) sys_dup,
    [42]  = (syscall_t) sys_pipe,
    [43]  = (syscall_t) sys_times,
    [45]  = (syscall_t) sys_brk,
    [47]  = (syscall_t) sys_getgid,
    [49]  = (syscall_t) sys_geteuid,
    [50]  = (syscall_t) sys_getegid,
    [52]  = (syscall_t) sys_umount2,
    [54]  = (syscall_t) sys_ioctl,
    [57]  = (syscall_t) sys_setpgid,
    [60]  = (syscall_t) sys_umask,
    [61]  = (syscall_t) sys_chroot,
    [63]  = (syscall_t) sys_dup2,
    [64]  = (syscall_t) sys_getppid,
    [65]  = (syscall_t) sys_getpgrp,
    [75]  = (syscall_t) sys_setrlimit,
    [76]  = (syscall_t) sys_getrlimit,
    [77]  = (syscall_t) sys_getrusage,
    [78]  = (syscall_t) sys_gettimeofday,
    [79]  = (syscall_t) sys_settimeofday,
    [83]  = (syscall_t) sys_symlink,
    [85]  = (syscall_t) sys_readlink,
    [90]  = (syscall_t) sys_mmap,
    [91]  = (syscall_t) sys_munmap,
    [94]  = (syscall_t) sys_fchmod,
    [102] = (syscall_t) sys_socketcall,
    [104] = (syscall_t) sys_setitimer,
    [114] = (syscall_t) sys_wait4,
    [116] = (syscall_t) sys_sysinfo,
    [118] = (syscall_t) sys_fsync,
    [120] = (syscall_t) sys_clone,
    [122] = (syscall_t) sys_uname,
    [125] = (syscall_t) sys_mprotect,
    [132] = (syscall_t) sys_getpgid,
    [133] = (syscall_t) sys_fchdir,
    [140] = (syscall_t) sys__llseek,
    [142] = (syscall_t) sys_select,
    [143] = (syscall_t) sys_flock,
    [145] = (syscall_t) sys_readv,
    [146] = (syscall_t) sys_writev,
    [147] = (syscall_t) sys_getsid,
    [162] = (syscall_t) sys_nanosleep,
    [163] = (syscall_t) sys_mremap,
    [168] = (syscall_t) sys_poll,
    [173] = (syscall_t) sys_rt_sigreturn,
    [174] = (syscall_t) sys_rt_sigaction,
    [175] = (syscall_t) sys_rt_sigprocmask,
    [180] = (syscall_t) sys_pread,
    [183] = (syscall_t) sys_getcwd,
    [186] = (syscall_t) sys_sigaltstack,
    [187] = (syscall_t) sys_sendfile,
    [190] = (syscall_t) sys_vfork,
    [192] = (syscall_t) sys_mmap2,
    [193] = (syscall_t) sys_truncate64,
    [194] = (syscall_t) sys_ftruncate64,
    [195] = (syscall_t) sys_stat64,
    [196] = (syscall_t) sys_lstat64,
    [197] = (syscall_t) sys_fstat64,
    [199] = (syscall_t) sys_getuid32,
    [200] = (syscall_t) sys_getgid32,
    [201] = (syscall_t) sys_geteuid32,
    [202] = (syscall_t) sys_getegid32,
    [206] = (syscall_t) syscall_stub,
    [207] = (syscall_t) sys_fchown32,
    [208] = (syscall_t) sys_setresuid,
    [210] = (syscall_t) sys_setresgid,
    [212] = (syscall_t) sys_chown32,
    [219] = (syscall_t) sys_madvise,
    [220] = (syscall_t) sys_getdents64,
    [221] = (syscall_t) sys_fcntl64,
    [224] = (syscall_t) sys_gettid,
    [228] = (syscall_t) sys_fsetxattr,
    [238] = (syscall_t) sys_tkill,
    [239] = (syscall_t) sys_sendfile64,
    [240] = (syscall_t) sys_futex,
    [242] = (syscall_t) sys_sched_getaffinity,
    [243] = (syscall_t) sys_set_thread_area,
    [252] = (syscall_t) sys_exit_group,
    [258] = (syscall_t) sys_set_tid_address,
    [264] = (syscall_t) sys_clock_settime,
    [265] = (syscall_t) sys_clock_gettime,
    [268] = (syscall_t) sys_statfs64,
    [269] = (syscall_t) sys_fstatfs64,
    [295] = (syscall_t) sys_openat,
    [296] = (syscall_t) sys_mkdirat,
    [298] = (syscall_t) sys_fchownat,
    [300] = (syscall_t) sys_fstatat64,
    [301] = (syscall_t) sys_unlinkat,
    [302] = (syscall_t) sys_renameat,
    [303] = (syscall_t) sys_linkat,
    [304] = (syscall_t) sys_symlinkat,
    [305] = (syscall_t) sys_readlinkat,
    [306] = (syscall_t) sys_fchmodat,
    [307] = (syscall_t) sys_faccessat,
    [308] = (syscall_t) sys_pselect,
    [320] = (syscall_t) sys_utimensat,
    [324] = (syscall_t) sys_fallocate,
    [340] = (syscall_t) sys_prlimit,
    [355] = (syscall_t) sys_getrandom,
};

void handle_interrupt(int interrupt) {
    TRACE_(instr, "\n");
    struct cpu_state *cpu = &current->cpu;
    if (interrupt == INT_SYSCALL) {
        int syscall_num = cpu->eax;
        if (syscall_num >= NUM_SYSCALLS || syscall_table[syscall_num] == NULL) {
            printk("%d missing syscall %d\n", current->pid, syscall_num);
            send_signal(current, SIGSYS_);
        } else {
            STRACE("%d call %-3d ", current->pid, syscall_num);
            int result = syscall_table[syscall_num](cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi, cpu->ebp);
            STRACE(" = 0x%x\n", result);
            cpu->eax = result;
        }
    } else if (interrupt == INT_GPF) {
        printk("%d page fault on 0x%x at 0x%x\n", current->pid, cpu->segfault_addr, cpu->eip);
        deliver_signal(current, SIGSEGV_);
    } else if (interrupt == INT_UNDEFINED) {
        printk("%d illegal instruction at 0x%x: ", current->pid, cpu->eip);
        for (int i = 0; i < 8; i++) {
            uint8_t b;
            if (user_get(cpu->eip + i, b))
                break;
            printk("%02x ", b);
        }
        printk("\n");
        deliver_signal(current, SIGILL_);
    } else if (interrupt != INT_TIMER) {
        printk("%d unhandled interrupt %d\n", current->pid, interrupt);
        sys_exit(interrupt);
    }
    receive_signals();
}

void dump_stack() {
    for (int i = 0; i < 64; i++) {
        dword_t stackword;
        if (user_get(current->cpu.esp + (i * 4), stackword))
            break;
        printk("%08x ", stackword);
        if (i % 8 == 7)
            printk("\n");
    }
}

// TODO find a home for this
#ifdef LOG_OVERRIDE
int log_override = 0;
#endif
