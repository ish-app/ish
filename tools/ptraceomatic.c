// Fun little utility that single-steps a program using ptrace and
// simultaneously runs the program in ish, and asserts that everything's
// working the same.
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#undef PAGE_SIZE // defined in sys/user.h, but we want the version from emu/memory.h
#include <sys/personality.h>

#include "sys/calls.h"
#include "sys/errno.h"
#include "emu/process.h"
#include "emu/cpuid.h"

#include "sys/exec/elf.h"
#include "tools/transplant.h"
#include "tools/ptutil.h"
#include "libvdso.so.h"

int compare_cpus(struct cpu_state *cpu, int pid) {
    struct user_regs_struct regs;
    trycall(ptrace(PTRACE_GETREGS, pid, NULL, &regs), "ptrace getregs compare");
#define CHECK_REG(pt, cp) \
    if (regs.pt != cpu->cp) { \
        printf(#pt " = 0x%llx, " #cp " = 0x%x\n", regs.pt, cpu->cp); \
        return -1; \
    }
    CHECK_REG(rax, eax);
    CHECK_REG(rbx, ebx);
    CHECK_REG(rcx, ecx);
    CHECK_REG(rdx, edx);
    CHECK_REG(rsi, esi);
    CHECK_REG(rdi, edi);
    CHECK_REG(rsp, esp);
    CHECK_REG(rbp, ebp);
    CHECK_REG(rip, eip);

    return 0;
    // compare pages marked dirty
    int fd = open_mem(pid);
    for (unsigned i = 0; i < PT_SIZE; i++) {
        if (cpu->mem.pt[i] && cpu->mem.pt[i]->dirty) {
            cpu->mem.pt[i]->dirty = 0;
            char page[PAGE_SIZE];
            trycall(lseek(fd, i<<PAGE_BITS, SEEK_SET), "compare seek mem");
            trycall(read(fd, page, PAGE_SIZE), "compare read mem");
            if (memcmp(page, cpu->mem.pt[i]->data, PAGE_SIZE) != 0) {
                printf("page %x doesn't match\n", i);
                debugger;
                return -1;
            }
        }
    }
    close(fd);
    return 0;
}

void step_tracing(struct cpu_state *cpu, int pid) {
    // step fake cpu
    int interrupt;
restart:
    interrupt = cpu_step32(cpu);
    if (interrupt != INT_NONE) {
        // hack to clean up before the exit syscall
        if (interrupt == INT_SYSCALL && cpu->eax == 1) {
            if (kill(pid, SIGKILL) < 0) {
                perror("kill tracee during exit");
                exit(1);
            }
        }
        if (handle_interrupt(cpu, interrupt)) {
            goto restart;
        }
    }

    // step real cpu
    // intercept cpuid, rdtsc, and int $0x80, though
    struct user_regs_struct regs;
    errno = 0;
    trycall(ptrace(PTRACE_GETREGS, pid, NULL, &regs), "ptrace getregs step");
    long inst = trycall(ptrace(PTRACE_PEEKTEXT, pid, regs.rip, NULL), "ptrace get inst step");

    if ((inst & 0xff) == 0x0f) {
        if (((inst & 0xff00) >> 8) == 0xa2) {
            // cpuid
            do_cpuid((dword_t *) &regs.rax, (dword_t *) &regs.rbx, (dword_t *) &regs.rcx, (dword_t *) &regs.rdx);
            regs.rip += 2;
        } else if (((inst & 0xff00) >> 8) == 0x31) {
            // rdtsc, no good way to get the same result here except copy from fake cpu
            regs.rax = cpu->eax;
            regs.rdx = cpu->edx;
            regs.rip += 2;
        } else {
            goto do_step;
        }
    } else if ((inst & 0xff) == 0xcd && ((inst & 0xff00) >> 8) == 0x80) {
        // int $0x80, consider intercepting the syscall
        dword_t syscall_num = (dword_t) regs.rax;
        if (syscall_num == 122) {
            // uname
            addr_t uname_ptr = (addr_t) regs.rbx;
            struct uname un;
            regs.rax = sys_uname(&un);
            pt_copy(pid, uname_ptr, &un, sizeof(struct uname));
        } else if (syscall_num == 85) {
            // readlink
            regs.rax = sys_readlink(0,0,0);
        } else if (syscall_num == 197) {
            // fstat64
            regs.rax = (uint32_t) _ENOSYS;
        } else {
            goto do_step;
        }
        regs.rip += 2;
    } else {
do_step: (void)0;
        // single step on a repeated string instruction only does one
        // iteration, so loop until ip changes
        long ip = regs.rip;
        while (regs.rip == ip) {
            trycall(ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL), "ptrace step");
            trycall(wait(NULL), "wait step");
            trycall(ptrace(PTRACE_GETREGS, pid, NULL, &regs), "ptrace getregs step");
        }
    }
    trycall(ptrace(PTRACE_SETREGS, pid, NULL, &regs), "ptrace setregs step");
}

void prepare_tracee(int pid) {
    transplant_vdso(pid, vdso_data, sizeof(vdso_data));
    aux_write(pid, AX_HWCAP, 0); // again, suck that
    aux_write(pid, AX_UID, 0);
    aux_write(pid, AX_EUID, 0);
    aux_write(pid, AX_GID, 0);
    aux_write(pid, AX_EGID, 0);

    // copy random bytes
    addr_t random = aux_read(pid, AX_RANDOM);
    for (addr_t a = random; a < random + 16; a += sizeof(dword_t)) {
        pt_write(pid, a, user_get(a));
    }
}

int main(int argc, char *const args[]) {
    int err;
    current = process_create();
    char *const argv[] = {args[1], NULL};
    char *const envp[] = {NULL};
    if ((err = sys_execve(args[1], argv, envp)) < 0) {
        return -err;
    }

    int pid = start_tracee(args[1], argv, envp);
    prepare_tracee(pid);

    struct cpu_state *cpu = &current->cpu;
    while (true) {
        struct cpu_state old_cpu = *cpu;
        step_tracing(cpu, pid);
        if (compare_cpus(cpu, pid) < 0) {
            printf("failure: resetting cpu\n");
            *cpu = old_cpu;
            __asm__("int3");
            cpu_step32(cpu);
            return -1;
        }
    }
}

// useful for calling from the debugger
void dump_memory(int pid, const char *file, addr_t start, addr_t end) {
    FILE *f = fopen(file, "w");
    for (addr_t addr = start; addr <= end; addr += sizeof(dword_t)) {
        dword_t val = pt_read(pid, addr);
        fwrite(&val, sizeof(dword_t), 1, f);
    }
    fclose(f);
}
