#include "debug.h"
#include "kernel/calls.h"
#include "emu/interrupt.h"

dword_t syscall_stub() {
    return _ENOSYS;
}
dword_t syscall_success_stub() {
    return 0;
}

syscall_t syscall_table[] = {
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
    [14]  = (syscall_t) sys_mknod,
    [15]  = (syscall_t) sys_chmod,
    [19]  = (syscall_t) sys_lseek,
    [20]  = (syscall_t) sys_getpid,
    [21]  = (syscall_t) sys_mount,
    [23]  = (syscall_t) sys_setuid,
    [24]  = (syscall_t) sys_getuid,
    [25]  = (syscall_t) sys_stime,
    [27]  = (syscall_t) sys_alarm,
    [29]  = (syscall_t) sys_pause,
    [30]  = (syscall_t) sys_utime,
    [33]  = (syscall_t) sys_access,
    [36]  = (syscall_t) syscall_success_stub, // sync
    [37]  = (syscall_t) sys_kill,
    [38]  = (syscall_t) sys_rename,
    [39]  = (syscall_t) sys_mkdir,
    [40]  = (syscall_t) sys_rmdir,
    [41]  = (syscall_t) sys_dup,
    [42]  = (syscall_t) sys_pipe,
    [43]  = (syscall_t) sys_times,
    [45]  = (syscall_t) sys_brk,
    [46]  = (syscall_t) sys_setgid,
    [47]  = (syscall_t) sys_getgid,
    [49]  = (syscall_t) sys_geteuid,
    [50]  = (syscall_t) sys_getegid,
    [52]  = (syscall_t) sys_umount2,
    [54]  = (syscall_t) sys_ioctl,
    [55]  = (syscall_t) sys_fcntl32,
    [57]  = (syscall_t) sys_setpgid,
    [60]  = (syscall_t) sys_umask,
    [61]  = (syscall_t) sys_chroot,
    [63]  = (syscall_t) sys_dup2,
    [64]  = (syscall_t) sys_getppid,
    [65]  = (syscall_t) sys_getpgrp,
    [66]  = (syscall_t) sys_setsid,
    [74]  = (syscall_t) sys_sethostname,
    [75]  = (syscall_t) sys_setrlimit32,
    [76]  = (syscall_t) sys_old_getrlimit32,
    [77]  = (syscall_t) sys_getrusage,
    [78]  = (syscall_t) sys_gettimeofday,
    [79]  = (syscall_t) sys_settimeofday,
    [80]  = (syscall_t) sys_getgroups,
    [81]  = (syscall_t) sys_setgroups,
    [83]  = (syscall_t) sys_symlink,
    [85]  = (syscall_t) sys_readlink,
    [88]  = (syscall_t) sys_reboot,
    [90]  = (syscall_t) sys_mmap,
    [91]  = (syscall_t) sys_munmap,
    [94]  = (syscall_t) sys_fchmod,
    [96]  = (syscall_t) sys_getpriority,
    [97]  = (syscall_t) sys_setpriority,
    [99]  = (syscall_t) sys_statfs,
    [100] = (syscall_t) sys_fstatfs,
    [102] = (syscall_t) sys_socketcall,
    [103] = (syscall_t) sys_syslog,
    [104] = (syscall_t) sys_setitimer,
    [114] = (syscall_t) sys_wait4,
    [116] = (syscall_t) sys_sysinfo,
    [117] = (syscall_t) sys_ipc,
    [118] = (syscall_t) sys_fsync,
    [119] = (syscall_t) sys_sigreturn,
    [120] = (syscall_t) sys_clone,
    [122] = (syscall_t) sys_uname,
    [125] = (syscall_t) sys_mprotect,
    [132] = (syscall_t) sys_getpgid,
    [133] = (syscall_t) sys_fchdir,
    [140] = (syscall_t) sys__llseek,
    [141] = (syscall_t) sys_getdents,
    [142] = (syscall_t) sys_select,
    [143] = (syscall_t) sys_flock,
    [144] = (syscall_t) sys_msync,
    [145] = (syscall_t) sys_readv,
    [146] = (syscall_t) sys_writev,
    [147] = (syscall_t) sys_getsid,
    [148] = (syscall_t) sys_fsync, // fdatasync
    [150] = (syscall_t) sys_mlock,
    [158] = (syscall_t) sys_sched_yield,
    [162] = (syscall_t) sys_nanosleep,
    [163] = (syscall_t) sys_mremap,
    [168] = (syscall_t) sys_poll,
    [172] = (syscall_t) sys_prctl,
    [173] = (syscall_t) sys_rt_sigreturn,
    [174] = (syscall_t) sys_rt_sigaction,
    [175] = (syscall_t) sys_rt_sigprocmask,
    [176] = (syscall_t) sys_rt_sigpending,
    [179] = (syscall_t) sys_rt_sigsuspend,
    [180] = (syscall_t) sys_pread,
    [183] = (syscall_t) sys_getcwd,
    [184] = (syscall_t) sys_capget,
    [185] = (syscall_t) sys_capset,
    [186] = (syscall_t) sys_sigaltstack,
    [187] = (syscall_t) sys_sendfile,
    [190] = (syscall_t) sys_vfork,
    [191] = (syscall_t) sys_getrlimit32,
    [192] = (syscall_t) sys_mmap2,
    [193] = (syscall_t) sys_truncate64,
    [194] = (syscall_t) sys_ftruncate64,
    [195] = (syscall_t) sys_stat64,
    [196] = (syscall_t) sys_lstat64,
    [197] = (syscall_t) sys_fstat64,
    [198] = (syscall_t) sys_lchown,
    [199] = (syscall_t) sys_getuid32,
    [200] = (syscall_t) sys_getgid32,
    [201] = (syscall_t) sys_geteuid32,
    [202] = (syscall_t) sys_getegid32,
    [205] = (syscall_t) sys_getgroups,
    [206] = (syscall_t) sys_setgroups,
    [207] = (syscall_t) sys_fchown32,
    [208] = (syscall_t) sys_setresuid,
    [209] = (syscall_t) sys_getresuid,
    [210] = (syscall_t) sys_setresgid,
    [211] = (syscall_t) sys_getresgid,
    [212] = (syscall_t) sys_chown32,
    [213] = (syscall_t) sys_setuid,
    [214] = (syscall_t) sys_setgid,
    [219] = (syscall_t) sys_madvise,
    [220] = (syscall_t) sys_getdents64,
    [221] = (syscall_t) sys_fcntl,
    [224] = (syscall_t) sys_gettid,
    [226 ... 237] = (syscall_t) sys_xattr_stub,
    [238] = (syscall_t) sys_tkill,
    [239] = (syscall_t) sys_sendfile64,
    [240] = (syscall_t) sys_futex,
    [241] = (syscall_t) sys_sched_setaffinity,
    [242] = (syscall_t) sys_sched_getaffinity,
    [243] = (syscall_t) sys_set_thread_area,
    [252] = (syscall_t) sys_exit_group,
    [254] = (syscall_t) sys_epoll_create0,
    [255] = (syscall_t) sys_epoll_ctl,
    [256] = (syscall_t) sys_epoll_wait,
    [258] = (syscall_t) sys_set_tid_address,
    [264] = (syscall_t) sys_clock_settime,
    [265] = (syscall_t) sys_clock_gettime,
    [266] = (syscall_t) sys_clock_getres,
    [268] = (syscall_t) sys_statfs64,
    [269] = (syscall_t) sys_fstatfs64,
    [270] = (syscall_t) sys_tgkill,
    [271] = (syscall_t) sys_utimes,
    [272] = (syscall_t) syscall_success_stub,
    [274] = (syscall_t) sys_mbind,
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
    [309] = (syscall_t) sys_ppoll,
    [311] = (syscall_t) sys_set_robust_list,
    [312] = (syscall_t) sys_get_robust_list,
    [319] = (syscall_t) sys_epoll_pwait,
    [320] = (syscall_t) sys_utimensat,
    [322] = (syscall_t) sys_timerfd_create,
    [323] = (syscall_t) sys_eventfd,
    [324] = (syscall_t) sys_fallocate,
    [328] = (syscall_t) sys_eventfd2,
    [329] = (syscall_t) sys_epoll_create,
    [330] = (syscall_t) sys_dup3,
    [331] = (syscall_t) sys_pipe2,
    [340] = (syscall_t) sys_prlimit64,
    [345] = (syscall_t) sys_sendmmsg,
    [353] = (syscall_t) sys_renameat2,
    [355] = (syscall_t) sys_getrandom,
    [359] = (syscall_t) sys_socket,
    [360] = (syscall_t) sys_socketpair,
    [361] = (syscall_t) sys_bind,
    [362] = (syscall_t) sys_connect,
    [363] = (syscall_t) sys_listen,
    [365] = (syscall_t) sys_getsockopt,
    [366] = (syscall_t) sys_setsockopt,
    [367] = (syscall_t) sys_getsockname,
    [368] = (syscall_t) sys_getpeername,
    [369] = (syscall_t) sys_sendto,
    [370] = (syscall_t) sys_sendmsg,
    [371] = (syscall_t) sys_recvfrom,
    [372] = (syscall_t) sys_recvmsg,
    [373] = (syscall_t) sys_shutdown,
    [375] = (syscall_t) syscall_stub, // membarrier
    [377] = (syscall_t) sys_copy_file_range,
    [383] = (syscall_t) syscall_stub,
    [384] = (syscall_t) sys_arch_prctl,
};

#define NUM_SYSCALLS (sizeof(syscall_table) / sizeof(syscall_table[0]))

void handle_interrupt(int interrupt) {
    TRACE_(instr, "\n");
    struct cpu_state *cpu = &current->cpu;
    if (interrupt == INT_SYSCALL) {
        unsigned syscall_num = cpu->eax;
        if (syscall_num >= NUM_SYSCALLS || syscall_table[syscall_num] == NULL) {
            printk("%d missing syscall %d\n", current->pid, syscall_num);
            deliver_signal(current, SIGSYS_, SIGINFO_NIL);
        } else {
            STRACE("%d call %-3d ", current->pid, syscall_num);
            int result = syscall_table[syscall_num](cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi, cpu->ebp);
            STRACE(" = 0x%x\n", result);
            cpu->eax = result;
        }
    } else if (interrupt == INT_GPF) {
        printk("%d page fault on 0x%x at 0x%x\n", current->pid, cpu->segfault_addr, cpu->eip);
        struct siginfo_ info = {
            .code = mem_segv_reason(cpu->mem, cpu->segfault_addr, cpu->segfault_type),
            .fault.addr = cpu->segfault_addr,
        };
        deliver_signal(current, SIGSEGV_, info);
    } else if (interrupt == INT_UNDEFINED) {
        printk("%d illegal instruction at 0x%x: ", current->pid, cpu->eip);
        for (int i = 0; i < 8; i++) {
            uint8_t b;
            if (user_get(cpu->eip + i, b))
                break;
            printk("%02x ", b);
        }
        printk("\n");
        struct siginfo_ info = {
            .code = SI_KERNEL_,
            .fault.addr = cpu->eip,
        };
        deliver_signal(current, SIGILL_, info);
    } else if (interrupt != INT_TIMER) {
        printk("%d unhandled interrupt %d\n", current->pid, interrupt);
        sys_exit(interrupt);
    }

    receive_signals();
    struct tgroup *group = current->group;
    lock(&group->lock);
    while (group->stopped)
        wait_for_ignore_signals(&group->stopped_cond, &group->lock, NULL);
    unlock(&group->lock);
}

void dump_stack() {
    printk("stack at %x, base at %x, ip at %x\n", current->cpu.esp, current->cpu.ebp, current->cpu.eip);
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
