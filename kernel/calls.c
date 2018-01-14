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
    [47]  = (syscall_t) sys_getgid,
    [33]  = (syscall_t) sys_access,
    [37]  = (syscall_t) sys_kill,
    [38]  = (syscall_t) sys_rename,
    [39]  = (syscall_t) sys_mkdir,
    [40]  = (syscall_t) sys_rmdir,
    [41]  = (syscall_t) sys_dup,
    [42]  = (syscall_t) sys_pipe,
    [45]  = (syscall_t) sys_brk,
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
    [83]  = (syscall_t) sys_symlink,
    [85]  = (syscall_t) sys_readlink,
    [90]  = (syscall_t) sys_mmap,
    [91]  = (syscall_t) sys_munmap,
    [94]  = (syscall_t) sys_fchmod,
    [102] = (syscall_t) sys_socketcall,
    [104] = (syscall_t) sys_setitimer,
    [114] = (syscall_t) sys_wait4,
    [116] = (syscall_t) sys_sysinfo,
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
    [162] = (syscall_t) sys_nanosleep,
    [168] = (syscall_t) sys_poll,
    [183] = (syscall_t) sys_getcwd,
    [173] = (syscall_t) sys_rt_sigreturn,
    [174] = (syscall_t) sys_rt_sigaction,
    [175] = (syscall_t) sys_rt_sigprocmask,
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
    [219] = (syscall_t) sys_madvise,
    [220] = (syscall_t) sys_getdents64,
    [221] = (syscall_t) sys_fcntl64,
    [224] = (syscall_t) sys_gettid,
    [228] = (syscall_t) sys_fsetxattr,
    [238] = (syscall_t) sys_tkill,
    [239] = (syscall_t) sys_sendfile64,
    [243] = (syscall_t) sys_set_thread_area,
    [252] = (syscall_t) sys_exit_group,
    [258] = (syscall_t) sys_set_tid_address,
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
    [306] = (syscall_t) sys_fchmodat,
    [320] = (syscall_t) sys_utimensat,
    [324] = (syscall_t) sys_fallocate,
    [340] = (syscall_t) sys_prlimit,
};

void handle_interrupt(int interrupt) {
    TRACELN_(instr, "\n");
    TRACE("int %d ", interrupt);
    struct cpu_state *cpu = &current->cpu;
    if (interrupt == INT_SYSCALL) {
        int syscall_num = cpu->eax;
        if (syscall_num >= NUM_SYSCALLS || syscall_table[syscall_num] == NULL) {
            println("missing syscall %d", syscall_num);
            send_signal(current, SIGSYS_);
        } else {
            STRACE("%d call %-3d ", current->pid, syscall_num);
            int result = syscall_table[syscall_num](cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi, cpu->ebp);
            STRACELN(" = 0x%x", result);
            cpu->eax = result;
        }
    } else if (interrupt == INT_GPF) {
        println("page fault at %x", cpu->segfault_addr);
        deliver_signal(current, SIGSEGV_);
    } else if (interrupt == INT_UNDEFINED) {
        println("illegal instruction");
        deliver_signal(current, SIGILL_);
    } else if (interrupt != INT_TIMER) {
        println("exiting");
        sys_exit(interrupt);
    }
    receive_signals();
}
