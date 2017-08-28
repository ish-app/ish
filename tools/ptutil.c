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
        perror(msg); printf("\r\n"); exit(1);
    }
    return res;
}

int start_tracee(const char *path, char *const argv[], char *const envp[]) {
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
        trycall(execve(path, argv, envp), "fexecve");
    } else {
        // parent, wait for child to stop after exec
        int status;
        trycall(wait(&status), "wait");
        if (!WIFSTOPPED(status)) {
            fprintf(stderr, "child failed to start\n");
            exit(1);
        }
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
