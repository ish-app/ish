// Runs a program simultaneously in ish and unicorn, single steps, and asserts
// everything is the same. Basically the same deal as ptraceomatic, except
// ptraceomatic doesn't run on my raspberry pi and I need to verify the damn
// thing still works on a raspberry pi.
// Oh and hopefully the code is somewhat less messy.
#include <stdio.h>
#include <errno.h>

#include <unicorn/unicorn.h>

#include "debug.h"
#include "kernel/calls.h"
#include "emu/interrupt.h"
#include "xX_main_Xx.h"

#include "undefined-flags.h"

long trycall(long res, const char *msg) {
    if (res == -1 && errno != 0) {
        perror(msg); printf("\r\n"); exit(1);
    }
    return res;
}

void uc_trycall(uc_err res, const char *msg) {
    if (res != UC_ERR_OK) {
        printf("%s: %s\r\n", msg, uc_strerror(res));
        exit(1);
    }
}

int compare_cpus(struct cpu_state *cpu, struct tlb *tlb, uc_engine *uc, int undefined_flags) {
    dword_t uc_reg_val;
    collapse_flags(cpu);
#define CHECK(real, fake, name) \
    if ((real) != (fake)) { \
        println(name ": real 0x%llx, fake 0x%llx", (unsigned long long) (real), (unsigned long long) (fake)); \
        debugger; \
        return -1; \
    }
#define CHECK_REG(uc_reg, cpu_reg) \
    uc_trycall(uc_reg_read(uc, UC_X86_REG_##uc_reg, &uc_reg_val), "uc_reg_read " #cpu_reg); \
    CHECK(uc_reg_val, cpu->cpu_reg, #cpu_reg)
    CHECK_REG(EAX, eax);
    CHECK_REG(EBX, ebx);
    CHECK_REG(ECX, ecx);
    CHECK_REG(EDX, edx);
    CHECK_REG(ESI, esi);
    CHECK_REG(EDI, edi);
    CHECK_REG(ESP, esp);
    CHECK_REG(EBP, ebp);
    CHECK_REG(EIP, eip);
    return 0;
}

void step_tracing(struct cpu_state *cpu, struct tlb *tlb, uc_engine *uc) {
    // step ish
    int changes = cpu->mem->changes;
    int interrupt = cpu_step32(cpu, tlb);
    if (interrupt != INT_NONE) {
        cpu->trapno = interrupt;
        handle_interrupt(interrupt);
    }
    if (cpu->mem->changes != changes)
        tlb_flush(tlb);

    // step unicorn
    uc_trycall(uc_emu_start(uc, cpu->eip, -1, 0, 1), "unicorn step");
}

uc_engine *start_unicorn(struct cpu_state *cpu, struct mem *mem) {
    uc_engine *uc;
    uc_trycall(uc_open(UC_ARCH_X86, UC_MODE_32, &uc), "uc_open");

#define copy_reg(cpu_reg, uc_reg) \
    uc_trycall(uc_reg_write(uc, UC_X86_REG_##uc_reg, &cpu->cpu_reg), "uc_reg_write " #cpu_reg)
    copy_reg(esp, ESP);
    copy_reg(eip, EIP);

    return uc;
}

int main(int argc, char *const argv[]) {
    int err = xX_main_Xx(argc, argv, NULL);
    if (err < 0) {
        // FIXME this often prints the wrong error message on non-x86_64
        fprintf(stderr, "%s\n", strerror(-err));
        return err;
    }

    // create a unicorn and set it up exactly the same as the current process
    uc_engine *uc = start_unicorn(&current->cpu, current->cpu.mem);

    struct cpu_state *cpu = &current->cpu;
    struct tlb *tlb = tlb_new(cpu->mem);
    int undefined_flags = 0;
    struct cpu_state old_cpu = *cpu;
    while (true) {
        if (compare_cpus(cpu, tlb, uc, undefined_flags) < 0) {
            println("failure: resetting cpu");
            *cpu = old_cpu;
            debugger;
            cpu_step32(cpu, tlb);
            return -1;
        }
        undefined_flags = undefined_flags_mask(cpu, tlb);
        old_cpu = *cpu;
        step_tracing(cpu, tlb, uc);
    }
}
