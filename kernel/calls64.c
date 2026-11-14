// System call table for the x86_64 guest (spike).
//
// x86_64 Linux numbers its system calls differently from i386 and has no
// "32-bit compatibility" variants (stat64, _llseek, mmap2, fcntl64, ...).
// Where the existing sys_ function already has the right arguments it is used
// directly; the guest-visible structures it reads and writes were widened in
// their headers (uword_t). The wrappers below cover calls whose arguments are
// shaped differently on x86_64.
#if ISH_GUEST_64
#include "kernel/calls.h"
#include "kernel/signal.h"
#include "kernel/resource.h"
#include "kernel/time.h"
#include "kernel/ptrace.h"
#include "fs/fd.h"

sqword_t sys64_rt_sigreturn(void);

static addr_t sys64_mmap(addr_t addr, qword_t len, dword_t prot, dword_t flags, fd_t fd, qword_t offset) {
    if (offset & (PAGE_SIZE - 1))
        return _EINVAL;
    return mmap_common(addr, len, prot, flags, fd, (off_t_) offset);
}

static sqword_t sys64_lseek(fd_t f, sqword_t off, dword_t whence) {
    struct fd *fd = f_get(f);
    if (fd == NULL)
        return _EBADF;
    if (!fd->ops->lseek)
        return _ESPIPE;
    lock(&fd->lock);
    STRACE("lseek(%d, %lld, %d)", f, (long long) off, whence);
    off_t_ res = fd->ops->lseek(fd, off, whence);
    unlock(&fd->lock);
    return res;
}

static dword_t sys64_ftruncate(fd_t f, qword_t size) {
    return sys_ftruncate64(f, (dword_t) size, (dword_t) (size >> 32));
}
static dword_t sys64_truncate(addr_t path, qword_t size) {
    return sys_truncate64(path, (dword_t) size, (dword_t) (size >> 32));
}

// x86_64 order is (flags, stack, ptid, ctid, tls)
static dword_t sys64_clone(dword_t flags, addr_t stack, addr_t ptid, addr_t ctid, addr_t tls) {
    return sys_clone(flags, stack, ptid, tls, ctid);
}

static dword_t sys64_getrlimit(dword_t resource, addr_t rlim_addr) {
    return sys_prlimit64(0, resource, 0, rlim_addr);
}
static dword_t sys64_setrlimit(dword_t resource, addr_t rlim_addr) {
    return sys_prlimit64(0, resource, rlim_addr, 0);
}

static dword_t sys64_enosys_quiet(void) {
    return _ENOSYS;
}
static dword_t sys64_success(void) {
    return 0;
}

static dword_t sys64_fallocate(fd_t f, dword_t mode, qword_t offset, qword_t len) {
    return sys_fallocate(f, mode, (dword_t) offset, (dword_t) (offset >> 32), (dword_t) len, (dword_t) (len >> 32));
}
// preadv2/pwritev2: no flags; offset -1 means "at the file position"
static dword_t sys64_preadv2(fd_t f, addr_t iov, dword_t count, qword_t pos, qword_t pos_h, dword_t flags) {
    if (flags != 0)
        return _EOPNOTSUPP;
    if ((sqword_t) pos == -1)
        return sys_readv(f, iov, count);
    return sys_preadv(f, iov, count, pos, pos_h);
}
static dword_t sys64_pwritev2(fd_t f, addr_t iov, dword_t count, qword_t pos, qword_t pos_h, dword_t flags) {
    if (flags != 0)
        return _EOPNOTSUPP;
    if ((sqword_t) pos == -1)
        return sys_writev(f, iov, count);
    return sys_pwritev(f, iov, count, pos, pos_h);
}
// the current fs uid/gid is all these can report: there is no separate one
static dword_t sys64_setfsuid(void) {
    return current->euid;
}
static dword_t sys64_setfsgid(void) {
    return current->egid;
}

#define C(fn) {(syscall64_t) (fn), false}
#define W(fn) {(syscall64_t) (fn), true}

const struct syscall64 syscall_table64[] = {
    [0] = C(sys_read),
    [1] = C(sys_write),
    [2] = C(sys_open),
    [3] = C(sys_close),
    [4] = C(sys_stat64),
    [5] = C(sys_fstat64),
    [6] = C(sys_lstat64),
    [7] = C(sys_poll),
    [8] = W(sys64_lseek),
    [9] = W(sys64_mmap),
    [10] = C(sys_mprotect),
    [11] = C(sys_munmap),
    [12] = W(sys_brk),
    [13] = C(sys_rt_sigaction),
    [14] = C(sys_rt_sigprocmask),
    [15] = W(sys64_rt_sigreturn),
    [16] = C(sys_ioctl),
    [17] = C(sys_pread),
    [18] = C(sys_pwrite),
    [19] = C(sys_readv),
    [20] = C(sys_writev),
    [21] = C(sys_access),
    [22] = C(sys_pipe),
    [23] = C(sys_select),
    [24] = C(sys_sched_yield),
    [25] = W(sys_mremap),
    [26] = C(sys_msync),
    [28] = C(sys_madvise),
    [32] = C(sys_dup),
    [33] = C(sys_dup2),
    [34] = C(sys_pause),
    [35] = C(sys_nanosleep),
    [37] = C(sys_alarm),
    [38] = C(sys_setitimer),
    [39] = C(sys_getpid),
    [40] = C(sys_sendfile64),
    [41] = C(sys_socket),
    [42] = C(sys_connect),
    [44] = C(sys_sendto),
    [45] = C(sys_recvfrom),
    [43] = C(sys_accept),
    [46] = C(sys_sendmsg),
    [47] = C(sys_recvmsg),
    [48] = C(sys_shutdown),
    [49] = C(sys_bind),
    [50] = C(sys_listen),
    [51] = C(sys_getsockname),
    [52] = C(sys_getpeername),
    [53] = C(sys_socketpair),
    [54] = C(sys_setsockopt),
    [55] = C(sys_getsockopt),
    [56] = C(sys64_clone),
    [57] = C(sys_fork),
    [58] = C(sys_vfork),
    [59] = C(sys_execve),
    [60] = C(sys_exit),
    [61] = C(sys_wait4),
    [62] = C(sys_kill),
    [63] = C(sys_uname),
    [72] = C(sys_fcntl),
    [73] = C(sys_flock),
    [74] = C(sys_fsync),
    [75] = C(sys_fsync),
    [76] = C(sys64_truncate),
    [77] = C(sys64_ftruncate),
    [79] = C(sys_getcwd),
    [80] = C(sys_chdir),
    [81] = C(sys_fchdir),
    [82] = C(sys_rename),
    [83] = C(sys_mkdir),
    [84] = C(sys_rmdir),
    [86] = C(sys_link),
    [87] = C(sys_unlink),
    [88] = C(sys_symlink),
    [89] = C(sys_readlink),
    [90] = C(sys_chmod),
    [91] = C(sys_fchmod),
    [92] = C(sys_chown32),
    [93] = C(sys_fchown32),
    [94] = C(sys_lchown),
    [95] = C(sys_umask),
    [96] = C(sys_gettimeofday),
    [97] = C(sys64_getrlimit),
    [98] = C(sys_getrusage),
    [99] = C(sys_sysinfo),
    [100] = C(sys_times),
    [102] = C(sys_getuid32),
    [104] = C(sys_getgid32),
    [105] = C(sys_setuid),
    [106] = C(sys_setgid),
    [107] = C(sys_geteuid32),
    [108] = C(sys_getegid32),
    [109] = C(sys_setpgid),
    [110] = C(sys_getppid),
    [111] = C(sys_getpgrp),
    [112] = C(sys_setsid),
    [113] = C(sys_setreuid),
    [114] = C(sys_setregid),
    [115] = C(sys_getgroups),
    [116] = C(sys_setgroups),
    [117] = C(sys_setresuid),
    [118] = C(sys_getresuid),
    [119] = C(sys_setresgid),
    [120] = C(sys_getresgid),
    [121] = C(sys_getpgid),
    [124] = C(sys_getsid),
    [125] = C(sys_capget),
    [126] = C(sys_capset),
    [127] = C(sys_rt_sigpending),
    [130] = C(sys_rt_sigsuspend),
    [128] = C(sys_rt_sigtimedwait),
    [131] = C(sys_sigaltstack),
    [133] = C(sys_mknod),
    [135] = C(sys_personality),
    [137] = C(sys_statfs),
    [138] = C(sys_fstatfs),
    [140] = C(sys_getpriority),
    [141] = C(sys_setpriority),
    [157] = C(sys_prctl),
    [158] = C(sys_arch_prctl),
    [160] = C(sys64_setrlimit),
    [161] = C(sys_chroot),
    [165] = C(sys_mount),
    [166] = C(sys_umount2),
    [169] = C(sys_reboot),
    [170] = C(sys_sethostname),
    [186] = C(sys_gettid),
    [200] = C(sys_tkill),
    [201] = C(sys_time),
    [202] = C(sys_futex),
    [203] = C(sys_sched_setaffinity),
    [204] = C(sys_sched_getaffinity),
    [213] = C(sys_epoll_create0),
    [217] = C(sys_getdents64),
    [218] = C(sys_set_tid_address),
    [228] = C(sys_clock_gettime),
    [229] = C(sys_clock_getres),
    [231] = C(sys_exit_group),
    [232] = C(sys_epoll_wait),
    [233] = C(sys_epoll_ctl),
    [234] = C(sys_tgkill),
    [235] = C(sys_utimes),
    [257] = C(sys_openat),
    [258] = C(sys_mkdirat),
    [259] = C(sys_mknodat),
    [260] = C(sys_fchownat),
    [262] = C(sys_fstatat64),
    [263] = C(sys_unlinkat),
    [264] = C(sys_renameat),
    [265] = C(sys_linkat),
    [266] = C(sys_symlinkat),
    [267] = C(sys_readlinkat),
    [268] = C(sys_fchmodat),
    [269] = C(sys_faccessat),
    [270] = C(sys_pselect),
    [271] = C(sys_ppoll),
    [273] = C(sys_set_robust_list),
    [274] = C(sys_get_robust_list),
    [280] = C(sys_utimensat),
    [281] = C(sys_epoll_pwait),
    [284] = C(sys_eventfd),
    [253] = C(sys_inotify_init),
    [254] = C(sys_inotify_add_watch),
    [255] = C(sys_inotify_rm_watch),
    [294] = C(sys_inotify_init1),
    [290] = C(sys_eventfd2),
    [291] = C(sys_epoll_create),
    [292] = C(sys_dup3),
    [293] = C(sys_pipe2),
    [302] = C(sys_prlimit64),
    [316] = C(sys_renameat2),
    [318] = C(sys_getrandom),
    [332] = C(sys_statx),
    [307] = C(sys_sendmmsg),
    [334] = C(sys64_enosys_quiet), // rseq
    // added with the interpreter
    [101] = C(sys_ptrace),
    [36] = C(sys_getitimer),
    [78] = C(sys_getdents),
    [103] = C(sys_syslog),
    [122] = C(sys64_setfsuid),
    [123] = C(sys64_setfsgid),
    [132] = C(sys_utime),
    [143] = C(sys_sched_getparam),
    [144] = C(sys_sched_setscheduler),
    [145] = C(sys_sched_getscheduler),
    [146] = C(sys_sched_get_priority_max),
    [147] = C(sys_sched_get_priority_min),
    [148] = C(sys_sched_rr_get_interval),
    [149] = C(sys_mlock),
    [150] = C(sys64_success), // munlock
    [151] = C(sys64_success), // mlockall
    [152] = C(sys64_success), // munlockall
    [162] = C(sys64_success), // sync
    [164] = C(sys_settimeofday),
    [187] = C(sys64_success), // readahead
    [188] = C(sys_xattr_stub), [189] = C(sys_xattr_stub), [190] = C(sys_xattr_stub),
    [191] = C(sys_xattr_stub), [192] = C(sys_xattr_stub), [193] = C(sys_xattr_stub),
    [194] = C(sys_xattr_stub), [195] = C(sys_xattr_stub), [196] = C(sys_xattr_stub),
    [197] = C(sys_xattr_stub), [198] = C(sys_xattr_stub), [199] = C(sys_xattr_stub),
    [221] = C(sys64_success), // fadvise64
    [222] = C(sys_timer_create),
    [223] = C(sys_timer_settime),
    [224] = C(sys_timer_gettime),
    [225] = C(sys_timer_getoverrun),
    [226] = C(sys_timer_delete),
    [227] = C(sys_clock_settime),
    [230] = C(sys_clock_nanosleep),
    [237] = C(sys_mbind),
    [247] = C(sys_waitid),
    [251] = C(sys_ioprio_set),
    [252] = C(sys_ioprio_get),
    [275] = C(sys_splice),
    [277] = C(sys64_success), // sync_file_range
    [283] = C(sys_timerfd_create),
    [285] = C(sys64_fallocate),
    [286] = C(sys_timerfd_settime),
    [287] = C(sys_timerfd_gettime),
    [288] = C(sys_accept4),
    [295] = C(sys_preadv),
    [296] = C(sys_pwritev),
    [306] = C(sys64_success), // syncfs
    [309] = C(sys_getcpu),
    [319] = C(sys_memfd_create),
    [324] = C(sys64_enosys_quiet), // membarrier
    [325] = C(sys_mlock), // mlock2
    [326] = C(sys_copy_file_range),
    [327] = C(sys64_preadv2),
    [328] = C(sys64_pwritev2),
    [435] = C(sys64_enosys_quiet), // clone3: libc falls back to clone
    [424] = C(sys_pidfd_send_signal),
    [434] = C(sys_pidfd_open),
    [436] = C(sys_close_range),
    [439] = C(sys_faccessat2),
};
const unsigned num_syscalls64 = sizeof(syscall_table64) / sizeof(syscall_table64[0]);
#endif
