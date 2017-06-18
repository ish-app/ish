#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/personality.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sched.h>
#include <syscall.h>
#undef PAGE_SIZE // want definition from emu/memory.h
#include "../misc.h"

long trycall(long res, const char *msg) {
    if (res == -1 && errno != 0) {
        perror(msg); exit(1);
    }
    return res;
}

int start_tracee(const char *program, char *const argv[], char *const envp[]) {
    // shut off aslr
    int persona = personality(0xffffffff);
    persona |= ADDR_NO_RANDOMIZE;
    personality(persona);

    int pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        // child
        trycall(ptrace(PTRACE_TRACEME, 0, NULL, NULL), "ptrace traceme");
        trycall(execve(program, argv, envp), "execl");
    } else {
        // parent, wait for child to stop after exec
        trycall(wait(NULL), "wait");
    }
    return pid;
}

int open_mem(int pid) {
    char filename[1024];
    sprintf(filename, "/proc/%d/mem", pid);
    return trycall(open(filename, O_RDWR), "open mem");
}

void pt_readn(int pid, addr_t addr, void *buf, size_t count) {
    int fd = open_mem(pid);
    trycall(lseek(fd, addr, SEEK_SET), "read seek");
    trycall(read(fd, buf, count), "read read");
    close(fd);
}

void pt_writen(int pid, addr_t addr, void *buf, size_t count) {
    int fd = open_mem(pid);
    trycall(lseek(fd, addr, SEEK_SET), "write seek");
    trycall(write(fd, buf, count), "write write");
    close(fd);
}

dword_t pt_read(int pid, addr_t addr) {
    dword_t res;
    pt_readn(pid, addr, &res, sizeof(res));
    return res;
}

void pt_write(int pid, addr_t addr, dword_t val) {
    pt_writen(pid, addr, &val, sizeof(val));
}

void pt_write8(int pid, addr_t addr, byte_t val) {
    pt_writen(pid, addr, &val, sizeof(val));
}

static addr_t aux_addr(int pid, int type) {
    struct user_regs_struct regs;
    trycall(ptrace(PTRACE_GETREGS, pid, NULL, &regs), "ptrace get sp for aux");
    dword_t sp = (dword_t) regs.rsp;
    // skip argc
    sp += 4;
    // skip argv
    while (pt_read(pid, sp) != 0)
        sp += 4;
    sp += 4;
    // skip envp
    while (pt_read(pid, sp) != 0)
        sp += 4;
    sp += 4;
    // dig through auxv
    dword_t aux_type;
    while ((aux_type = pt_read(pid, sp)) != 0) {
        sp += 4;
        if (aux_type == type) {
            return sp;
        }
        sp += 4;
    }
    return 0;
}

dword_t aux_read(int pid, int type) {
    return pt_read(pid, aux_addr(pid, type));
}
void aux_write(int pid, int type, dword_t value) {
    return pt_write(pid, aux_addr(pid, type), value);
}
