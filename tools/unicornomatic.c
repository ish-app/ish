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

// unicorn wrappers

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

uint32_t uc_getreg(uc_engine *uc, int reg_id) {
    uint32_t value;
    uc_trycall(uc_reg_read(uc, reg_id, &value), "uc_getreg");
    return value;
}

void uc_setreg(uc_engine *uc, int reg_id, uint32_t value) {
    uc_trycall(uc_reg_write(uc, reg_id, &value), "uc_setreg");
}

void uc_read(uc_engine *uc, addr_t addr, void *buf, size_t size) {
    uc_trycall(uc_mem_read(uc, addr, buf, size), "uc_read");
}

void uc_write(uc_engine *uc, addr_t addr, void *buf, size_t size) {
    uc_trycall(uc_mem_write(uc, addr, buf, size), "uc_write");
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

#define CHECK(uc, ish, name) \
    if ((uc) != (ish)) { \
        println(name ": uc 0x%llx, ish 0x%llx", (unsigned long long) (uc), (unsigned long long) (ish)); \
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
    // sync up the flags so undefined flags won't error out next time
    uc_setreg(uc, UC_X86_REG_EFLAGS, regs.eflags);

    // compare pages marked dirty
    if (tlb->dirty_page != TLB_PAGE_EMPTY) {
        char real_page[PAGE_SIZE];
        uc_trycall(uc_mem_read(uc, tlb->dirty_page, real_page, PAGE_SIZE), "compare read");
        struct pt_entry entry = cpu->mem->pt[PAGE(tlb->dirty_page)];
        void *fake_page = entry.data->data + entry.offset;

        if (memcmp(real_page, fake_page, PAGE_SIZE) != 0) {
            println("page %x doesn't match", tlb->dirty_page);
            debugger;
            return -1;
        }
        tlb->dirty_page = TLB_PAGE_EMPTY;
    }

    return 0;
}

static int uc_interrupt;

static void set_tls_pointer(uc_engine *uc, dword_t tls_ptr);

static void _mem_sync(struct tlb *tlb, uc_engine *uc, addr_t addr, dword_t size) {
    char buf[size];
    tlb_read(tlb, addr, buf, size);
    uc_write(uc, addr, buf, size);
}
#define mem_sync(addr, size) _mem_sync(tlb, uc, addr, size)

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
    uc_interrupt = -1;
    dword_t eip = uc_getreg(uc, UC_X86_REG_EIP);
    while (uc_getreg(uc, UC_X86_REG_EIP) == eip)
        uc_trycall(uc_emu_start(uc, eip, -1, 0, 1), "unicorn step");

    // handle unicorn interrupts
    struct uc_regs regs;
    uc_getregs(uc, &regs);
    if (uc_interrupt == 0x80) {
        uint32_t syscall_num = regs.eax;
        switch (syscall_num) {
            case 243: { // set_thread_area
                // icky hacky
                addr_t tls_ptr;
                uc_read(uc, regs.ebx + 4, &tls_ptr, sizeof(tls_ptr));
                set_tls_pointer(uc, tls_ptr);
                mem_sync(regs.ebx, 4);
                break;
            }
        }
        uc_setreg(uc, UC_X86_REG_EAX, cpu->eax);
    } else if (uc_interrupt != -1) {
        println("unhandled unicorn interrupt 0x%x", uc_interrupt);
        exit(1);
    }
}

static void uc_interrupt_callback(uc_engine *uc, uint32_t interrupt, void *user_data) {
    uc_interrupt = interrupt;
    uc_emu_stop(uc);
}

// thread local bullshit {{{
struct gdt_entry {
    uint16_t limit0;
    uint16_t base0;
    uint8_t base1;
    bits type:4;
    bits system:1;
    bits dpl:2;
    bits present:1;
    unsigned limit1:4;
    bits avail:1;
    bits is_64_code:1;
    bits db:1;
    bits granularity:1;
    uint8_t base2;
} __attribute__((packed));

// it has to go somewhere, so why not page 1, where nothing can go normally
#define GDT_ADDR 0x1000

static void setup_gdt(uc_engine *uc) {
    // construct gdt
    struct gdt_entry gdt[13] = {};
    // descriptor 0 can't be used
    // descriptor 1 = all of memory as code
    gdt[1].limit0 = 0xffff; gdt[1].limit1 = 0xf; gdt[1].granularity = 1;
    gdt[1].system = 1; gdt[1].type = 0xf; gdt[1].db = 1; gdt[1].present = 1;
    // descriptor 2 = all of memory as data
    gdt[2] = gdt[1]; gdt[2].type = 0x3;
    // descriptor 12 = thread locals
    gdt[12] = gdt[2]; gdt[12].dpl = 3;

    // put gdt into memory, somewhere, idgaf where
    uc_trycall(uc_mem_map(uc, GDT_ADDR, PAGE_SIZE, UC_PROT_READ), "map gdt");
    uc_trycall(uc_mem_write(uc, GDT_ADDR, &gdt, sizeof(gdt)), "write gdt");

    // load segment registers
    uc_x86_mmr gdtr = {.base = GDT_ADDR, .limit = sizeof(gdt)};
    uc_trycall(uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr), "write gdtr");
    uc_setreg(uc, UC_X86_REG_CS, 1<<3);
    uc_setreg(uc, UC_X86_REG_DS, 2<<3);
    uc_setreg(uc, UC_X86_REG_ES, 2<<3);
    uc_setreg(uc, UC_X86_REG_FS, 2<<3);
    uc_setreg(uc, UC_X86_REG_SS, 2<<3);
}

static void set_tls_pointer(uc_engine *uc, dword_t tls_ptr) {
    struct gdt_entry tls_entry;
    uc_read(uc, GDT_ADDR + 12 * sizeof(struct gdt_entry), &tls_entry, sizeof(tls_entry));
    tls_entry.base0 = (tls_ptr & 0x0000ffff);
    tls_entry.base1 = (tls_ptr & 0x00ff0000) >> 16;
    tls_entry.base2 = (tls_ptr & 0xff000000) >> 24;
    uc_write(uc, GDT_ADDR + 12 * sizeof(struct gdt_entry), &tls_entry, sizeof(tls_entry));
}
// }}}

uc_engine *start_unicorn(struct cpu_state *cpu, struct mem *mem) {
    uc_engine *uc;
    uc_trycall(uc_open(UC_ARCH_X86, UC_MODE_32, &uc), "uc_open");

    // copy registers
    uc_setreg(uc, UC_X86_REG_ESP, cpu->esp);
    uc_setreg(uc, UC_X86_REG_EIP, cpu->eip);
    uc_setreg(uc, UC_X86_REG_EFLAGS, cpu->eflags);

    // copy memory
    // XXX unicorn has a ?bug? where setting up 334 mappings takes five
    // seconds on my raspi. it seems to be accidentally quadratic (dot tumblr dot com)
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

    // set up some sort of gdt, because we need gs to work for thread locals
    setup_gdt(uc);

    // set up exception handler
    uc_hook hook;
    uc_trycall(uc_hook_add(uc, &hook, UC_HOOK_INTR, uc_interrupt_callback, NULL, 1, 0), "uc_hook_add");

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
    fclose(f);
}
