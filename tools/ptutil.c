#include <unistd.h>
#include <errno.h>
#include <sys/personality.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
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

dword_t pt_read(int pid, addr_t addr) {
    long res = ptrace(PTRACE_PEEKDATA, pid, addr, NULL);
    if (res < 0 && errno == EIO) {
        // this means the address is out of bounds. try backing it up 4 bytes and using the second half
        return trycall(ptrace(PTRACE_PEEKDATA, pid, addr - 4, NULL), "memory read") >> 32;
    }
    return trycall(res, "memory read");
}

void pt_write(int pid, addr_t addr, dword_t val) {
    // when a 64-bit process traces a 32-bit process, all writes are 64-bit. gah
    uint64_t out = ((uint64_t) pt_read(pid, addr + 4)) << 32 | val;
    trycall(ptrace(PTRACE_POKEDATA, pid, addr, out), "memory write");
}

static void pt_write8(int pid, addr_t addr, byte_t val) {
    // when a 64-bit process traces a 32-bit process, all writes are 64-bit. gah
    uint64_t thingy = trycall(ptrace(PTRACE_PEEKDATA, pid, addr + 1), "memory write read");
    uint64_t out = thingy << 8 | val;
    trycall(ptrace(PTRACE_POKEDATA, pid, addr, out), "memory write");
}

void pt_copy(int pid, addr_t addr, const void *vdata, size_t len) {
    const byte_t *data = (byte_t *) vdata;
    for (int i = 0; i < len; i++) {
        pt_write8(pid, addr + i, data[i]);
    }
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
