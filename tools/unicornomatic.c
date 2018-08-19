// Runs a program simultaneously in ish and unicorn, single steps, and asserts
// everything is the same. Basically the same deal as ptraceomatic, except
// ptraceomatic doesn't run on my raspberry pi and I need to verify the damn
// thing still works on a raspberry pi.
// Oh and hopefully the code is somewhat less messy.
#include <stdio.h>
#include <errno.h>

#include <unicorn/unicorn.h>

#include "misc.h"
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

struct uc_regs {
    dword_t eax;
    dword_t ebx;
    dword_t ecx;
    dword_t edx;
    dword_t esi;
    dword_t edi;
    dword_t ebp;
    dword_t esp;
    dword_t eip;
    dword_t eflags;
};
void uc_getregs(uc_engine *uc, struct uc_regs *regs) {
    static int uc_regs_ids[] = {
        UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
        UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP,
        UC_X86_REG_EIP, UC_X86_REG_EFLAGS,
    };
    void *ptrs[sizeof(uc_regs_ids)/sizeof(uc_regs_ids[0])] = {
        &regs->eax, &regs->ebx, &regs->ecx, &regs->edx,
        &regs->esi, &regs->edi, &regs->ebp, &regs->esp,
        &regs->eip, &regs->eflags,
    };
    uc_trycall(uc_reg_read_batch(uc, uc_regs_ids, ptrs, sizeof(ptrs)/sizeof(ptrs[0])), "uc_reg_read_batch");
}

int compare_cpus(struct cpu_state *cpu, struct tlb *tlb, uc_engine *uc, int undefined_flags) {
    struct uc_regs regs;
    uc_getregs(uc, &regs);
    collapse_flags(cpu);

#define CHECK(real, fake, name) \
    if ((real) != (fake)) { \
        println(name ": real 0x%llx, fake 0x%llx", (unsigned long long) (real), (unsigned long long) (fake)); \
        debugger; \
        return -1; \
    }

#define CHECK_REG(reg) \
    CHECK(regs.reg, cpu->reg, #reg)
    CHECK_REG(eip);
    CHECK_REG(eax);
    CHECK_REG(ebx);
    CHECK_REG(ecx);
    CHECK_REG(edx);
    CHECK_REG(esi);
    CHECK_REG(edi);
    CHECK_REG(esp);
    CHECK_REG(ebp);

    // check the flags, with a nice visual representation
    regs.eflags = (regs.eflags & ~undefined_flags) | (cpu->eflags & undefined_flags);
    if (regs.eflags != cpu->eflags) {
#define f(x,n) ((regs.eflags & (1 << n)) ? #x : "-"),
        printf("real eflags = 0x%x %s%s%s%s%s%s%s%s%s, fake eflags = 0x%x %s%s%s%s%s%s%s%s%s\r\n%0d",
                regs.eflags, f(o,11)f(d,10)f(i,9)f(t,8)f(s,7)f(z,6)f(a,4)f(p,2)f(c,0)
#undef f
#define f(x,n) ((cpu->eflags & (1 << n)) ? #x : "-"),
                cpu->eflags, f(o,11)f(d,10)f(i,9)f(t,8)f(s,7)f(z,6)f(a,4)f(p,2)f(c,0)0);
        debugger;
        return -1;
    }
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
    dword_t eip;
    uc_trycall(uc_reg_read(uc, UC_X86_REG_EIP, &eip), "unicorn read eip");
    uc_trycall(uc_emu_start(uc, eip, -1, 0, 1), "unicorn step");
}

uc_engine *start_unicorn(struct cpu_state *cpu, struct mem *mem) {
    uc_engine *uc;
    uc_trycall(uc_open(UC_ARCH_X86, UC_MODE_32, &uc), "uc_open");

    // copy registers
#define copy_reg(cpu_reg, uc_reg) \
    uc_trycall(uc_reg_write(uc, UC_X86_REG_##uc_reg, &cpu->cpu_reg), "uc_reg_write " #cpu_reg)
    copy_reg(esp, ESP);
    copy_reg(eip, EIP);
    copy_reg(eflags, EFLAGS);

    // copy memory
    for (page_t page = 0; page < MEM_PAGES; page++) {
        struct pt_entry *pt = &mem->pt[page];
        if (pt->data == NULL)
            continue;
        int prot = UC_PROT_READ | UC_PROT_EXEC;
        // really only the write bit is meaningful (FIXME)
        if (pt->flags & P_WRITE) prot |= UC_PROT_WRITE;
        addr_t addr = page << PAGE_BITS;
        void *data = pt->data->data + pt->offset;
        uc_trycall(uc_mem_map(uc, addr, PAGE_SIZE, prot), "uc_mem_map");
        uc_trycall(uc_mem_write(uc, addr, data, PAGE_SIZE), "uc_mem_write");
    }

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

void dump_memory(uc_engine *uc, const char *file, addr_t start, size_t size) {
    char buf[size];
    uc_trycall(uc_mem_read(uc, start, buf, size), "uc_mem_read");
    FILE *f = fopen(file, "w");
    fwrite(buf, 1, sizeof(buf), f);
}
