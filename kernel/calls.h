#ifndef CALLS_H
#define CALLS_H

#include "kernel/task.h"
#include "kernel/errno.h"
#include "fs/fd.h"
#include "kernel/fs.h"
#include "misc.h"

#include "kernel/signal.h"
#include "fs/sock.h"
#include "kernel/time.h"
#include "kernel/resource.h"

void handle_interrupt(int interrupt);

int must_check user_read(addr_t addr, void *buf, size_t count);
int must_check user_write(addr_t addr, const void *buf, size_t count);
int must_check user_read_task(struct task *task, addr_t addr, void *buf, size_t count);
int must_check user_write_task(struct task *task, addr_t addr, const void *buf, size_t count);
int must_check user_read_string(addr_t addr, char *buf, size_t max);
int must_check user_write_string(addr_t addr, const char *buf);
#define user_get(addr, var) user_read(addr, &(var), sizeof(var))
#define user_put(addr, var) user_write(addr, &(var), sizeof(var))
#define user_get_task(task, addr, var) user_read_task(task, addr, &(var), sizeof(var))
#define user_put_task(task, addr, var) user_write_task(task, addr, &(var), sizeof(var))

// process lifecycle
dword_t sys_clone(dword_t flags, addr_t stack, addr_t ptid, addr_t tls, addr_t ctid);
dword_t sys_fork(void);
dword_t sys_vfork(void);
int sys_execve(const char *file, char *const argv[], char *const envp[]);
dword_t _sys_execve(addr_t file, addr_t argv, addr_t envp);
dword_t sys_exit(dword_t status);
noreturn void do_exit(int status);
dword_t sys_exit_group(dword_t status);
dword_t sys_wait4(dword_t pid, addr_t status_addr, dword_t options, addr_t rusage_addr);
dword_t sys_waitpid(dword_t pid, addr_t status_addr, dword_t options);

// memory management
addr_t sys_brk(addr_t new_brk);

#define MMAP_SHARED 0x1
#define MMAP_PRIVATE 0x2
#define MMAP_FIXED 0x10
#define MMAP_ANONYMOUS 0x20
addr_t sys_mmap(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset);
addr_t sys_mmap2(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset);
int_t sys_munmap(addr_t addr, uint_t len);
int_t sys_mprotect(addr_t addr, uint_t len, int_t prot);
dword_t sys_madvise(addr_t addr, dword_t len, dword_t advice);

// file descriptor things
#define LSEEK_SET 0
#define LSEEK_CUR 1
#define LSEEK_END 2
#define LOCK_SH_ 1
#define LOCK_EX_ 2
#define LOCK_NB_ 4
#define LOCK_UN_ 8
struct io_vec {
    addr_t base;
    uint_t len;
};
dword_t sys_read(fd_t fd_no, addr_t buf_addr, dword_t size);
dword_t sys_readv(fd_t fd_no, addr_t iovec_addr, dword_t iovec_count);
dword_t sys_write(fd_t fd_no, addr_t buf_addr, dword_t size);
dword_t sys_writev(fd_t fd_no, addr_t iovec_addr, dword_t iovec_count);
dword_t sys__llseek(fd_t f, dword_t off_high, dword_t off_low, addr_t res_addr, dword_t whence);
dword_t sys_lseek(fd_t f, dword_t off, dword_t whence);
dword_t sys_ioctl(fd_t f, dword_t cmd, dword_t arg);
dword_t sys_fcntl64(fd_t f, dword_t cmd, dword_t arg);
dword_t sys_dup(fd_t fd);
dword_t sys_dup2(fd_t fd, fd_t new_fd);
dword_t sys_close(fd_t fd);
dword_t sys_fsync(fd_t f);
dword_t sys_flock(fd_t fd, dword_t operation);
dword_t sys_pipe(addr_t pipe_addr);

// file management
fd_t sys_open(addr_t path_addr, dword_t flags, mode_t_ mode);
fd_t sys_openat(fd_t at, addr_t path_addr, dword_t flags, mode_t_ mode);
dword_t sys_close(fd_t fd);
dword_t sys_link(addr_t src_addr, addr_t dst_addr);
dword_t sys_linkat(fd_t src_at_f, addr_t src_addr, fd_t dst_at_f, addr_t dst_addr);
dword_t sys_unlink(addr_t path_addr);
dword_t sys_unlinkat(fd_t at_f, addr_t path_addr);
dword_t sys_rmdir(addr_t path_addr);
dword_t sys_rename(addr_t src_addr, addr_t dst_addr);
dword_t sys_renameat(fd_t src_at_f, addr_t src_addr, fd_t dst_at_f, addr_t dst_addr);
dword_t sys_symlink(addr_t target_addr, addr_t link_addr);
dword_t sys_symlinkat(addr_t target_addr, fd_t at_f, addr_t link_addr);
dword_t sys_access(addr_t path_addr, dword_t mode);
dword_t sys_readlink(addr_t path, addr_t buf, dword_t bufsize);
dword_t sys_readlinkat(fd_t at_f, addr_t path, addr_t buf, dword_t bufsize);
int_t sys_getdents64(fd_t f, addr_t dirents, dword_t count);
dword_t sys_stat64(addr_t path_addr, addr_t statbuf_addr);
dword_t sys_lstat64(addr_t path_addr, addr_t statbuf_addr);
dword_t sys_fstat64(fd_t fd_no, addr_t statbuf_addr);
dword_t sys_fstatat64(fd_t at, addr_t path_addr, addr_t statbuf_addr, dword_t flags);
dword_t sys_fchmod(fd_t f, dword_t mode);
dword_t sys_fchmodat(fd_t at_f, addr_t path_addr, dword_t mode, int flags);
dword_t sys_chmod(addr_t path_addr, dword_t mode);
dword_t sys_fchown32(fd_t f, dword_t owner, dword_t group);
dword_t sys_fchownat(fd_t at_f, addr_t path_addr, dword_t owner, dword_t group, int flags);
dword_t sys_truncate64(addr_t path_addr, dword_t size_low, dword_t size_high);
dword_t sys_ftruncate64(fd_t f, dword_t size_low, dword_t size_high);
dword_t sys_fallocate(fd_t f, dword_t mode, dword_t offset_low, dword_t offset_high, dword_t len_low, dword_t len_high);
dword_t sys_mkdir(addr_t path_addr, mode_t_ mode);
dword_t sys_mkdirat(fd_t at_f, addr_t path_addr, mode_t_ mode);
dword_t sys_utimensat(fd_t at_f, addr_t path_addr, addr_t times_addr, dword_t flags);
dword_t sys_umask(dword_t mask);

dword_t sys_sendfile(fd_t out_fd, fd_t in_fd, addr_t offset_addr, dword_t count);
dword_t sys_sendfile64(fd_t out_fd, fd_t in_fd, addr_t offset_addr, dword_t count);

dword_t sys_statfs64(addr_t path_addr, addr_t buf_addr);
dword_t sys_fstatfs64(fd_t f, addr_t buf_addr);

dword_t sys_mount(addr_t source_addr, addr_t target_addr, addr_t type_addr, dword_t flags, addr_t data_addr);
dword_t sys_umount2(addr_t target_addr, dword_t flags);
dword_t sys_fsetxattr(addr_t path_addr, addr_t name_addr, addr_t value_addr, dword_t size, dword_t flags);

// process information
dword_t sys_getpid(void);
dword_t sys_gettid(void);
dword_t sys_getppid(void);
dword_t sys_getpgid(dword_t pid);
dword_t sys_setpgid(dword_t pid, dword_t pgid);
dword_t sys_getpgrp(void);
dword_t sys_setpgrp(void);
dword_t sys_getuid32(void);
dword_t sys_getuid(void);
dword_t sys_geteuid32(void);
dword_t sys_geteuid(void);
dword_t sys_getgid32(void);
dword_t sys_getgid(void);
dword_t sys_getegid32(void);
dword_t sys_getegid(void);
dword_t sys_setresuid(uid_t_ ruid, uid_t_ euid, uid_t_ suid);
dword_t sys_setresgid(uid_t_ rgid, uid_t_ egid, uid_t_ sgid);
dword_t sys_getcwd(addr_t buf_addr, dword_t size);
dword_t sys_chdir(addr_t path_addr);
dword_t sys_chroot(addr_t path_addr);
dword_t sys_fchdir(fd_t f);
int task_set_thread_area(struct task *task, addr_t u_info);
int sys_set_thread_area(addr_t u_info);
int sys_set_tid_address(addr_t blahblahblah);
dword_t sys_setsid(void);

// system information
#define UNAME_LENGTH 65
struct uname {
    char system[UNAME_LENGTH];   // Linux
    char hostname[UNAME_LENGTH]; // my-compotar
    char release[UNAME_LENGTH];  // 1.2.3-ish
    char version[UNAME_LENGTH];  // SUPER AWESOME
    char arch[UNAME_LENGTH];     // i686
    char domain[UNAME_LENGTH];   // lol
};
dword_t sys_uname(addr_t uts_addr);

struct sys_info {
    dword_t uptime;
    dword_t loads[3];
    dword_t totalram;
    dword_t freeram;
    dword_t sharedram;
    dword_t bufferram;
    dword_t totalswap;
    dword_t freeswap;
    word_t procs;
    dword_t totalhigh;
    dword_t freehigh;
    dword_t mem_unit;
    char pad[8];
};
dword_t sys_sysinfo(addr_t info_addr);

// futexes

// crap that ideally shouldn't exist
struct pollfd_ {
    fd_t fd;
    word_t events;
    word_t revents;
};
dword_t sys_poll(addr_t fds, dword_t nfds, dword_t timeout);
dword_t sys_select(fd_t nfds, addr_t readfds_addr, addr_t writefds_addr, addr_t exceptfds_addr, addr_t timeout_addr);

// misc
dword_t sys_futex(addr_t uaddr, dword_t op, dword_t val);
dword_t sys_getrandom(addr_t buf_addr, dword_t len, dword_t flags);

typedef int (*syscall_t)(dword_t, dword_t, dword_t, dword_t, dword_t, dword_t);

#endif
