// ptrace basics for a 64-bit tracee. Every line must end in OK, natively and in ish.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#define CHECK(name, cond) printf("%-40s %s\n", name, (cond) ? "OK" : "FAIL")
static volatile long magic = 0x1234;
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    pid_t pid = fork();
    if (pid == 0) {
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        raise(SIGSTOP);
        volatile long x = 0;
        for (int i = 0; i < 1000; i++) x += i;
        _exit(magic == 0xbeef ? 42 : 1);
    }
    int status;
    waitpid(pid, &status, 0);
    CHECK("child stopped with SIGSTOP", WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP);
    struct user_regs_struct regs; memset(&regs, 0, sizeof regs);
    CHECK("PTRACE_GETREGS", ptrace(PTRACE_GETREGS, pid, 0, &regs) == 0);
    CHECK("rip and rsp look like addresses", regs.rip > 0x1000 && regs.rsp > 0x1000 && (regs.rsp & 7) == 0);
    errno = 0;
    long peek = ptrace(PTRACE_PEEKDATA, pid, (void *) &magic, 0);
    CHECK("PTRACE_PEEKDATA reads 8 bytes", errno == 0 && peek == 0x1234);
    errno = 0;
    long urip = ptrace(PTRACE_PEEKUSER, pid, (void *) offsetof(struct user, regs.rip), 0);
    CHECK("PTRACE_PEEKUSER rip matches", errno == 0 && (unsigned long) urip == regs.rip);
    CHECK("PTRACE_POKEDATA", ptrace(PTRACE_POKEDATA, pid, (void *) &magic, (void *) 0xbeef) == 0);
    peek = ptrace(PTRACE_PEEKDATA, pid, (void *) &magic, 0);
    CHECK("poked value reads back", peek == 0xbeef);
    CHECK("tracer's own copy untouched", magic == 0x1234);
    struct user_fpregs_struct fp; memset(&fp, 0, sizeof fp);
    CHECK("PTRACE_GETFPREGS", ptrace(PTRACE_GETFPREGS, pid, 0, &fp) == 0 && (fp.mxcsr & 0x1f80) == 0x1f80 && (fp.cwd & 0x3f) == 0x3f);
    int steps = 0, moved = 0; unsigned long last = regs.rip;
    for (int i = 0; i < 20; i++) {
        if (ptrace(PTRACE_SINGLESTEP, pid, 0, 0) != 0) break;
        waitpid(pid, &status, 0);
        if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP) break;
        ptrace(PTRACE_GETREGS, pid, 0, &regs);
        steps++;
        if (regs.rip != last) moved++;
        last = regs.rip;
    }
    CHECK("20 single steps, each a SIGTRAP stop", steps == 20);
    CHECK("rip moved on every step", moved == 20);
    regs.r15 = 0x1122334455667788ul; regs.r14 = 0xffffffff00000001ul;
    CHECK("PTRACE_SETREGS", ptrace(PTRACE_SETREGS, pid, 0, &regs) == 0);
    struct user_regs_struct again; ptrace(PTRACE_GETREGS, pid, 0, &again);
    CHECK("registers read back as set", again.r15 == 0x1122334455667788ul && again.r14 == 0xffffffff00000001ul && again.rip == regs.rip);
    CHECK("PTRACE_CONT", ptrace(PTRACE_CONT, pid, 0, 0) == 0);
    waitpid(pid, &status, 0);
    CHECK("child saw the poked value and exited", WIFEXITED(status) && WEXITSTATUS(status) == 42);
    return 0;
}
