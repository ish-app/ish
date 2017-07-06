#ifndef CALLS_H
#define CALLS_H

#include "emu/process.h"
#include "sys/errno.h"
#include "sys/fs.h"
#include "misc.h"

#include "sys/signal.h"

int handle_interrupt(struct cpu_state *cpu, int interrupt);

dword_t user_get(addr_t addr);
byte_t user_get8(addr_t addr);
void user_put(addr_t addr, dword_t value);
void user_put8(addr_t addr, byte_t value);
int user_get_string(addr_t addr, char *buf, size_t max);
void user_put_string(addr_t addr, const char *buf);
int user_get_count(addr_t addr, void *buf, size_t count);
void user_put_count(addr_t addr, const void *buf, size_t count);

// process lifecycle
int sys_execve(const char *file, char *const argv[], char *const envp[]);
dword_t _sys_execve(addr_t file, addr_t argv, addr_t envp);
dword_t sys_exit(dword_t status);
dword_t sys_exit_group(dword_t status);

// memory management
addr_t sys_brk(addr_t new_brk);
int handle_pagefault(addr_t addr);

#define MMAP_SHARED 0x1
#define MMAP_PRIVATE 0x2
#define MMAP_FIXED 0x10
#define MMAP_ANONYMOUS 0x20
addr_t sys_mmap(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset);
addr_t sys_mmap2(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset);
int_t sys_munmap(addr_t addr, uint_t len);
int_t sys_mprotect(addr_t addr, uint_t len, int_t prot);

// file descriptor things
struct io_vec {
    addr_t base;
    uint_t len;
};
#define LSEEK_SET 0
#define LSEEK_CUR 1
#define LSEEK_END 2
dword_t sys_read(fd_t fd_no, addr_t buf_addr, dword_t size);
dword_t sys_write(fd_t fd_no, addr_t buf_addr, dword_t size);
dword_t sys_writev(fd_t fd_no, addr_t iovec_addr, dword_t iovec_count);
dword_t sys__llseek(fd_t f, dword_t off_high, dword_t off_low, addr_t res_addr, dword_t whence);
dword_t sys_lseek(fd_t f, dword_t off, dword_t whence);
dword_t sys_ioctl(fd_t f, dword_t cmd, dword_t arg);

dword_t sys_dup(fd_t fd);
dword_t sys_dup2(fd_t fd, fd_t new_fd);

// file management
#define MAX_PATH 4096
fd_t sys_open(addr_t pathname_addr, dword_t flags, dword_t mode);
dword_t sys_close(fd_t fd);
dword_t sys_unlink(addr_t pathname_addr);
dword_t sys_access(addr_t pathname_addr, dword_t mode);
dword_t sys_readlink(addr_t pathname, addr_t buf, dword_t bufsize);
int_t sys_getdents64(fd_t f, addr_t dirents, dword_t count);
dword_t sys_stat64(addr_t pathname_addr, addr_t statbuf_addr);
dword_t sys_lstat64(addr_t pathname_addr, addr_t statbuf_addr);
dword_t sys_fstat64(fd_t fd_no, addr_t statbuf_addr);

dword_t sys_sendfile(fd_t out_fd, fd_t in_fd, addr_t offset_addr, dword_t count);
dword_t sys_sendfile64(fd_t out_fd, fd_t in_fd, addr_t offset_addr, dword_t count);

// process information
dword_t sys_getpid();
dword_t sys_getppid();
dword_t sys_getuid32();
dword_t sys_getuid();
dword_t sys_getgid32();
dword_t sys_getgid();
dword_t sys_getcwd(addr_t buf_addr, dword_t size);
int sys_set_thread_area(addr_t u_info);

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
int sys_uname(struct uname *uts);
dword_t _sys_uname(addr_t uts_addr);

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

// time
struct time_spec {
    dword_t sec;
    dword_t nsec;
};

dword_t sys_time(addr_t time_out);
#define CLOCK_REALTIME_ 0
#define CLOCK_MONOTONIC_ 1
dword_t sys_clock_gettime(dword_t clock, addr_t tp);

typedef int (*syscall_t)(dword_t,dword_t,dword_t,dword_t,dword_t,dword_t);

#endif
