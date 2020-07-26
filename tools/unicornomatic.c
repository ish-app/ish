// Runs a program simultaneously in ish and unicorn, single steps, and asserts
// everything is the same. Basically the same deal as ptraceomatic, except
// ptraceomatic doesn't run on my raspberry pi and I need to verify the damn
// thing still works on a raspberry pi.
// Oh and hopefully the code is somewhat less messy.
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>

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

static void uc_unmap(uc_engine *uc, addr_t start, dword_t size) {
    for (addr_t addr = start; addr < start + size; addr += PAGE_SIZE) {
        uc_mem_unmap(uc, addr, PAGE_SIZE); // ignore errors
    }
}
static void uc_map(uc_engine *uc, addr_t start, dword_t size) {
    uc_unmap(uc, start, size);
    for (addr_t addr = start; addr < start + size; addr += PAGE_SIZE) {
        uc_trycall(uc_mem_map(uc, addr, PAGE_SIZE, UC_PROT_ALL), "mmap emulation");
    }
}
static void uc_map_ptr(uc_engine *uc, addr_t start, void *mem, dword_t size) {
    uc_unmap(uc, start, size);
    for (addr_t addr = start; addr < start + size; addr += PAGE_SIZE) {
        uc_trycall(uc_mem_map_ptr(uc, addr, PAGE_SIZE, UC_PROT_ALL, mem + (addr - start)), "mmap emulation");
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
    word_t fpcw;
    word_t fpsw;
    float80 fp[8];
};
static int uc_regs_ids[] = {
    UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP,
    UC_X86_REG_EIP, UC_X86_REG_EFLAGS,
    UC_X86_REG_FPCW, UC_X86_REG_FPSW,
    UC_X86_REG_FP0, UC_X86_REG_FP1, UC_X86_REG_FP2, UC_X86_REG_FP3,
    UC_X86_REG_FP4, UC_X86_REG_FP5, UC_X86_REG_FP6, UC_X86_REG_FP7,
};
void uc_getregs(uc_engine *uc, struct uc_regs *regs) {
    void *ptrs[sizeof(uc_regs_ids)/sizeof(uc_regs_ids[0])] = {
        &regs->eax, &regs->ebx, &regs->ecx, &regs->edx,
        &regs->esi, &regs->edi, &regs->ebp, &regs->esp,
        &regs->eip, &regs->eflags,
        &regs->fpcw, &regs->fpsw,
        &regs->fp[0], &regs->fp[1], &regs->fp[2], &regs->fp[3],
        &regs->fp[4], &regs->fp[5], &regs->fp[6], &regs->fp[7],
    };
    uc_trycall(uc_reg_read_batch(uc, uc_regs_ids, ptrs, sizeof(ptrs)/sizeof(ptrs[0])), "uc_reg_read_batch");
}
void uc_setregs(uc_engine *uc, struct uc_regs *regs) {
    void *const ptrs[sizeof(uc_regs_ids)/sizeof(uc_regs_ids[0])] = {
        &regs->eax, &regs->ebx, &regs->ecx, &regs->edx,
        &regs->esi, &regs->edi, &regs->ebp, &regs->esp,
        &regs->eip, &regs->eflags,
        &regs->fpcw, &regs->fpsw,
        &regs->fp[0], &regs->fp[1], &regs->fp[2], &regs->fp[3],
        &regs->fp[4], &regs->fp[5], &regs->fp[6], &regs->fp[7],
    };
    uc_trycall(uc_reg_write_batch(uc, uc_regs_ids, ptrs, sizeof(ptrs)/sizeof(ptrs[0])), "uc_reg_write_batch");
}

int compare_cpus(struct cpu_state *cpu, struct tlb *tlb, uc_engine *uc, int undefined_flags) {
    int res = 0;
    struct uc_regs regs;
    uc_getregs(uc, &regs);
    collapse_flags(cpu);

#define CHECK(uc, ish, name) \
    if ((uc) != (ish)) { \
        printk("check failed: " name ": uc 0x%llx, ish 0x%llx\n", (unsigned long long) (uc), (unsigned long long) (ish)); \
        res = -1; \
        ish = uc; \
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
        printk("real eflags = 0x%x %s%s%s%s%s%s%s%s%s, fake eflags = 0x%x %s%s%s%s%s%s%s%s%s\n%0d",
                regs.eflags, f(o,11)f(d,10)f(i,9)f(t,8)f(s,7)f(z,6)f(a,4)f(p,2)f(c,0)
#undef f
#define f(x,n) ((cpu->eflags & (1 << n)) ? #x : "-"),
                cpu->eflags, f(o,11)f(d,10)f(i,9)f(t,8)f(s,7)f(z,6)f(a,4)f(p,2)f(c,0)0);
        res = -1;
        cpu->eflags = regs.eflags;
    }
    // sync up the flags so undefined flags won't error out next time

#define FSW_MASK 0x7d00 // only look at top, c0, c2, c3
    regs.fpsw &= FSW_MASK;
    cpu->fsw &= FSW_MASK;
    CHECK(regs.fpsw, cpu->fsw, "fsw");
    CHECK(regs.fpcw, cpu->fcw, "fcw");

#define CHECK_FPREG(i) \
    CHECK(regs.fp[i].signif, cpu->fp[i].signif, "fp"#i" signif"); \
    CHECK(regs.fp[i].signExp, cpu->fp[i].signExp, "fp"#i" signExp")
    CHECK_FPREG(0);
    CHECK_FPREG(1);
    CHECK_FPREG(2);
    CHECK_FPREG(3);
    CHECK_FPREG(4);
    CHECK_FPREG(5);
    CHECK_FPREG(6);
    CHECK_FPREG(7);

    uc_setregs(uc, &regs);

    // compare pages marked dirty
    if (tlb->dirty_page != TLB_PAGE_EMPTY) {
        char real_page[PAGE_SIZE];
        uc_trycall(uc_mem_read(uc, tlb->dirty_page, real_page, PAGE_SIZE), "compare read");
        void *fake_page = mmu_translate(cpu->mmu, tlb->dirty_page, MEM_READ);

        if (memcmp(real_page, fake_page, PAGE_SIZE) != 0) {
            printk("page %x doesn't match\n", tlb->dirty_page);
            debugger;
            return -1;
        }
        tlb->dirty_page = TLB_PAGE_EMPTY;
    }

    return res;
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
    addr_t old_brk = current->mm->brk; // this is important
    int interrupt = cpu_run_to_interrupt(cpu, tlb);
    handle_interrupt(interrupt);

    // step unicorn
    uc_interrupt = -1;
    dword_t eip = uc_getreg(uc, UC_X86_REG_EIP);
    // intercept cpuid and rdtsc
    uint8_t code[2];
    uc_read(uc, eip, code, sizeof(code));
    if (code[0] == 0x0f && (code[1] == 0x31 || code[1] == 0xa2)) {
        if (code[1] == 0x31) {
            uc_setreg(uc, UC_X86_REG_EAX, cpu->eax);
            uc_setreg(uc, UC_X86_REG_EDX, cpu->edx);
        } else if (code[1] == 0xa2) {
            uc_setreg(uc, UC_X86_REG_EAX, cpu->eax);
            uc_setreg(uc, UC_X86_REG_EBX, cpu->ebx);
            uc_setreg(uc, UC_X86_REG_ECX, cpu->ecx);
            uc_setreg(uc, UC_X86_REG_EDX, cpu->edx);
        }
        uc_setreg(uc, UC_X86_REG_EIP, eip+2);
    } else {
        while (uc_getreg(uc, UC_X86_REG_EIP) == eip)
            uc_trycall(uc_emu_start(uc, eip, -1, 0, 1), "unicorn step");
    }

    // handle unicorn interrupts
    struct uc_regs regs;
    uc_getregs(uc, &regs);
    if (uc_interrupt == 0x80) {
        uint32_t syscall_num = regs.eax;
        switch (syscall_num) {
            // put syscall result from fake process into real process
            case 3: // read
                mem_sync(regs.ecx, cpu->edx); break;
            case 7: // waitpid
                mem_sync(regs.ecx, sizeof(dword_t)); break;
            case 13: // time
                if (regs.ebx != 0)
                    mem_sync(regs.ebx, sizeof(dword_t));
                break;
            case 54: { // ioctl (god help us)
                struct fd *fd = f_get(cpu->ebx);
                if (fd && fd->ops->ioctl_size) {
                    ssize_t ioctl_size = fd->ops->ioctl_size(cpu->ecx);
                    if (ioctl_size >= 0)
                        mem_sync(regs.edx, ioctl_size);
                }
                break;
            }
            case 85: // readlink
                mem_sync(regs.ecx, regs.edx); break;
            case 102: { // socketcall
                dword_t args[6];
                (void) user_get(regs.ecx, args);
                dword_t len;
                switch (cpu->ebx) {
                    case 6: // getsockname
                        (void) user_get(args[2], len);
                        mem_sync(args[1], len);
                        break;
                    case 8: // socketpair
                        mem_sync(args[3], sizeof(dword_t[2]));
                        break;
                    case 12: // recvfrom
                        mem_sync(args[1], args[2]);
                        (void) user_get(args[5], len);
                        mem_sync(args[4], len);
                        break;
                }
                break;
            }
            case 104: // setitimer
                mem_sync(regs.edx, sizeof(struct itimerval_)); break;
            case 116: // sysinfo
                mem_sync(regs.ebx, sizeof(struct sys_info)); break;
            case 122: // uname
                mem_sync(regs.ebx, sizeof(struct uname)); break;
            case 140: // _llseek
                mem_sync(regs.esi, 8); break;
            case 145: { // readv
                struct iovec_ vecs[regs.edx];
                (void) user_get(regs.ecx, vecs);
                for (unsigned i = 0; i < regs.edx; i++)
                    mem_sync(vecs[i].base, vecs[i].len);
                break;
            }
            case 162: // nanosleep
                mem_sync(regs.ecx, sizeof(struct timespec_)); break;
            case 168: // poll
                mem_sync(regs.ebx, sizeof(struct pollfd_) * regs.ecx); break;
            case 174: // rt_sigaction
                if (regs.edx)
                    mem_sync(regs.edx, sizeof(struct sigaction_));
                break;
            case 183: // getcwd
                mem_sync(regs.ebx, cpu->eax); break;

            case 195: // stat64
            case 196: // lstat64
            case 197: // fstat64
                mem_sync(regs.ecx, sizeof(struct newstat64)); break;
            case 300: // fstatat64
                mem_sync(regs.edx, sizeof(struct newstat64)); break;
            case 220: // getdents64
                mem_sync(regs.ecx, cpu->eax); break;
            case 265: // clock_gettime
                mem_sync(regs.ecx, sizeof(struct timespec_)); break;

            case 192: // mmap2
            case 90: // mmap
                if (cpu->eax >= 0xfffff000) {
                    // fake mmap failed, so don't try real mmap
                    break;
                }
                // IMPORTANT: if you try to understand this code you will get brain cancer
                addr_t start = cpu->eax;
                dword_t size = cpu->ecx;
                int prot = cpu->edx;
                struct fd *fd = f_get(cpu->edi);
                int real_fd = fd ? fd->real_fd : -1;
                int flags = cpu->esi & ~MAP_FIXED;
                off_t offset = cpu->ebp;
                if (syscall_num == 192)
                    offset <<= PAGE_BITS;
                void *mem = mmap(NULL, size, prot, flags, real_fd, offset);
                if (mem == MAP_FAILED) {
                    perror("mmap emulation");
                    exit(1);
                }
                uc_map_ptr(uc, start, mem, size);
                break;

            case 91: // munmap
                if ((int) cpu->eax >= 0)
                    uc_unmap(uc, cpu->ebx, cpu->ecx);
                break;

            case 45: // brk
                // matches up with the logic in kernel/mmap.c
                if (current->mm->brk > old_brk) {
                    uc_map(uc, BYTES_ROUND_UP(old_brk), BYTES_ROUND_UP(current->mm->brk) - BYTES_ROUND_UP(old_brk));
                } else if (current->mm->brk < old_brk) {
                    uc_unmap(uc, BYTES_ROUND_DOWN(current->mm->brk), BYTES_ROUND_DOWN(old_brk) - BYTES_ROUND_DOWN(current->mm->brk));
                }
                break;

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
        printk("unhandled unicorn interrupt 0x%x\n", uc_interrupt);
        exit(1);
    }
}

static void uc_interrupt_callback(uc_engine *uc, uint32_t interrupt, void *UNUSED(user_data)) {
    uc_interrupt = interrupt;
    uc_emu_stop(uc);
}

static bool uc_unmapped_callback(uc_engine *uc, uc_mem_type UNUSED(type), uint64_t address, int size, int64_t UNUSED(value), void *UNUSED(user_data)) {
    struct pt_entry *pt = mem_pt(current->mem, PAGE(address));
    // handle stack growing
    if (pt != NULL && pt->flags & P_GROWSDOWN) {
        uc_map(uc, BYTES_ROUND_DOWN(address), PAGE_SIZE);
        return true;
    }
    printk("unicorn reports unmapped access at 0x%lx size %d\n", address, size);
    return false;
}

// thread local bullshit {{{
struct gdt_entry {
    uint16_t limit0;
    uint16_t base0;
    uint8_t base1;
    bitfield type:4;
    bitfield system:1;
    bitfield dpl:2;
    bitfield present:1;
    unsigned limit1:4;
    bitfield avail:1;
    bitfield is_64_code:1;
    bitfield db:1;
    bitfield granularity:1;
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
    uc_setreg(uc, UC_X86_REG_FPCW, cpu->fcw);

    // copy memory
    // XXX unicorn has a ?bug? where setting up 334 mappings takes five
    // seconds on my raspi. it seems to be accidentally quadratic (dot tumblr dot com)
    for (page_t page = 0; page < MEM_PAGES; page++) {
        struct pt_entry *pt = mem_pt(mem, page);
        if (pt == NULL)
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
    uc_trycall(uc_hook_add(uc, &hook, UC_HOOK_MEM_UNMAPPED, uc_unmapped_callback, NULL, 1, 0), "uc_hook_add");

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
    uc_engine *uc = start_unicorn(&current->cpu, &current->mm->mem);

    struct cpu_state *cpu = &current->cpu;
    struct tlb tlb;
    tlb_refresh(&tlb, cpu->mmu);
    int undefined_flags = 0;
    struct cpu_state old_cpu = *cpu;
    while (true) {
        while (compare_cpus(cpu, &tlb, uc, undefined_flags) < 0) {
            printk("resetting cpu\n");
            *cpu = old_cpu;
            debugger;
            cpu_run_to_interrupt(cpu, &tlb);
        }
        undefined_flags = undefined_flags_mask(cpu, &tlb);
        old_cpu = *cpu;
        step_tracing(cpu, &tlb, uc);
    }
}

void dump_memory(uc_engine *uc, const char *file, addr_t start, size_t size) {
    char buf[size];
    uc_trycall(uc_mem_read(uc, start, buf, size), "uc_mem_read");
    FILE *f = fopen(file, "w");
    fwrite(buf, 1, sizeof(buf), f);
    fclose(f);
}

