// 64-bit guest spike: run x86_64 user code with unicorn (QEMU's TCG) behind
// the same cpu_run_to_interrupt interface asbestos provides.
//
// This is a correctness reference only. Unicorn is a JIT, so it cannot ship on
// iOS; it exists so the 64-bit kernel layer (loader, syscalls, signals) can be
// brought up and tested before a 64-bit interpreter exists.
//
// Guest memory stays in ish's own page tables. Pages are handed to unicorn
// lazily: an access to a page unicorn does not know faults into mem_hook(),
// which resolves it with mem_ptr() (doing copy-on-write / stack growth as
// usual) and maps that one host page. Whenever the address space changes
// (mmu->changes moves) every lazy mapping is dropped.
#include <unicorn/unicorn.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "emu/cpu.h"
#include "emu/tlb.h"
#include "emu/interrupt.h"
#include "kernel/memory.h"
#include "asbestos/asbestos.h"

// A run of guest pages handed to unicorn as one region. The pages are
// contiguous in guest memory AND in host memory and share one writability, so
// a single uc_mem_map_ptr covers them. Unicorn's cost is per region (each map
// or unmap rebuilds its memory view, and it cannot hold more than 4096), so
// the point is to keep the number of regions small.
struct uregion {
    addr_t start;
    pages_t pages;
    void *ptr;
    bool writable;
};

struct ucpu {
    uc_engine *uc;
    struct cpu_state *cpu;
    struct mmu *mmu;
    uint64_t changes;
    struct uregion *regions;
    size_t region_count, region_cap;
    // one bit per guest page: is it inside a region?
    uint8_t *mapped_bits;
    size_t pages_live;
    unsigned long n_faults;
    int interrupt;
    unsigned blocks;
    bool want_stop;
    bool stepping;
    bool remap_pending;
    // unicorn re-reports the aborted page fault on the next run
    bool stale_fault;
    addr_t remap_page;
};

static int stats_on = -1;
static size_t max_regions = 400;
#define UC_MAX_REGIONS max_regions
// how far a run may extend on each side of the faulting page
#define UC_RUN_MAX 2048

static pthread_mutex_t serial_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t ucpu_key;
static pthread_once_t ucpu_once = PTHREAD_ONCE_INIT;

static void ucpu_free(void *p) {
    struct ucpu *u = p;
    if (u == NULL)
        return;
    uc_close(u->uc);
    free(u->regions);
    free(u->mapped_bits);
    free(u);
}
static void ucpu_key_init(void) {
    pthread_key_create(&ucpu_key, ucpu_free);
}

#define uc_must(call) do { \
    uc_err _e = (call); \
    if (_e != UC_ERR_OK) { \
        fprintf(stderr, "unicorn64: %s: %s\n", #call, uc_strerror(_e)); \
        abort(); \
    } \
} while (0)

static bool is_mapped(struct ucpu *u, addr_t page_addr) {
    page_t page = PAGE(page_addr);
    return u->mapped_bits[page >> 3] & (1 << (page & 7));
}

static void mark_mapped(struct ucpu *u, addr_t start, pages_t pages, bool mapped) {
    for (page_t page = PAGE(start); page < PAGE(start) + pages; page++) {
        if (mapped)
            u->mapped_bits[page >> 3] |= 1 << (page & 7);
        else
            u->mapped_bits[page >> 3] &= ~(1 << (page & 7));
    }
}

static void region_drop(struct ucpu *u, struct uregion *r) {
    uc_mem_unmap(u->uc, r->start, (size_t) r->pages << PAGE_BITS);
    mark_mapped(u, r->start, r->pages, false);
    u->pages_live -= r->pages;
    *r = u->regions[--u->region_count];
}

static void unmap_all(struct ucpu *u) {
    while (u->region_count > 0)
        region_drop(u, &u->regions[u->region_count - 1]);
}

static void forget_mapped(struct ucpu *u, addr_t page_addr) {
    for (size_t i = 0; i < u->region_count; i++) {
        struct uregion *r = &u->regions[i];
        if (page_addr >= r->start && page_addr - r->start < (addr_t) r->pages << PAGE_BITS) {
            region_drop(u, r);
            return;
        }
    }
}

// Is this guest page backed by this host pointer with this writability?
// Pages still waiting for their copy-on-write copy never qualify: they are
// copied one at a time when touched (see mem_hook).
static bool page_matches(struct mem *mem, addr_t page_addr, void *ptr, bool writable) {
    struct pt_entry *entry = mem_pt(mem, PAGE(page_addr));
    if (entry == NULL)
        return false;
    if ((void *) ((char *) entry->data->data + entry->offset) != ptr)
        return false;
    if ((entry->flags & P_WRITE) && (entry->flags & P_COW))
        return false;
    return (bool) P_WRITABLE(entry->flags) == writable;
}

// Drop every region that no longer matches ish's page tables.
static void regions_validate(struct ucpu *u) {
    struct mem *mem = container_of(u->mmu, struct mem, mmu);
    for (size_t i = u->region_count; i-- > 0;) {
        struct uregion *r = &u->regions[i];
        for (pages_t n = 0; n < r->pages; n++) {
            addr_t off = (addr_t) n << PAGE_BITS;
            if (!page_matches(mem, r->start + off, (char *) r->ptr + off, r->writable)) {
                region_drop(u, r);
                break;
            }
        }
    }
}

// Map the page at page_addr (host memory at ptr) together with as many
// neighbors as can share its region.
static bool map_run(struct ucpu *u, struct mem *mem, addr_t page_addr, void *ptr) {
    struct pt_entry *entry = mem_pt(mem, PAGE(page_addr));
    bool writable = entry != NULL && P_WRITABLE(entry->flags);

    addr_t lo = page_addr, hi = page_addr;
    for (int n = 0; n < UC_RUN_MAX && lo >= PAGE_SIZE; n++) {
        addr_t cand = lo - PAGE_SIZE;
        if (is_mapped(u, cand) || !page_matches(mem, cand, (char *) ptr - (page_addr - cand), writable))
            break;
        lo = cand;
    }
    for (int n = 0; n < UC_RUN_MAX && PAGE(hi) + 1 < MEM_PAGES; n++) {
        addr_t cand = hi + PAGE_SIZE;
        if (is_mapped(u, cand) || !page_matches(mem, cand, (char *) ptr + (cand - page_addr), writable))
            break;
        hi = cand;
    }
    pages_t pages = PAGE(hi) - PAGE(lo) + 1;
    void *run_ptr = (char *) ptr - (page_addr - lo);

    uint32_t perms = UC_PROT_READ | UC_PROT_EXEC;
    if (writable)
        perms |= UC_PROT_WRITE;
    uc_err err = uc_mem_map_ptr(u->uc, lo, (size_t) pages << PAGE_BITS, perms, run_ptr);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "unicorn64: map %#x+%u pages failed: %s\n", lo, pages, uc_strerror(err));
        u->cpu->segfault_addr = page_addr;
        u->cpu->segfault_was_write = false;
        u->interrupt = INT_GPF;
        return false;
    }
    if (u->region_count == u->region_cap) {
        u->region_cap = u->region_cap ? u->region_cap * 2 : 256;
        u->regions = realloc(u->regions, u->region_cap * sizeof(struct uregion));
    }
    u->regions[u->region_count++] = (struct uregion) {.start = lo, .pages = pages, .ptr = run_ptr, .writable = writable};
    mark_mapped(u, lo, pages, true);
    u->pages_live += pages;
    // Over the cap: ask for a stop at the next block boundary (block_hook).
    // Stopping from inside a memory hook, in the middle of an instruction,
    // corrupted guest state.
    if (u->region_count > UC_MAX_REGIONS + UC_MAX_REGIONS / 3)
        u->want_stop = true;
    return true;
}

static bool mem_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data) {
    (void) size; (void) value;
    struct ucpu *u = user_data;
    struct cpu_state *cpu = u->cpu;
    u->n_faults++;
    bool write = type == UC_MEM_WRITE_UNMAPPED || type == UC_MEM_WRITE_PROT;

    if (address >> PAGE_BITS >= MEM_PAGES) {
        // outside the address space
        cpu->segfault_addr64 = 0;
        cpu->segfault_addr = address;
        cpu->segfault_was_write = write;
        u->interrupt = INT_GPF;
        return false;
    }

    struct mem *mem = container_of(u->mmu, struct mem, mmu);
    addr_t page_addr = (addr_t) address & ~(addr_t) (PAGE_SIZE - 1);
    // the mem read lock is held by task_run_current
    uint64_t changes_before = u->mmu->changes;
    // A copy-on-write page is copied on first touch, even a read. Otherwise it
    // would have to be mapped read-only and swapped on the first write, and
    // unicorn cannot have a mapping replaced from inside a protection hook.
    bool want_write = write;
    struct pt_entry *pt = mem_pt(mem, PAGE(page_addr));
    if (pt != NULL && (pt->flags & P_WRITE) && (pt->flags & P_COW))
        want_write = true;
    void *ptr = mem_ptr(mem, page_addr, want_write ? MEM_WRITE : MEM_READ);
    // mem_ptr may itself have changed the address space (copy-on-write, stack
    // growth). That only concerns this page, which is dealt with right here,
    // so it is not a reason to throw every other mapping away.
    if (u->changes == changes_before)
        u->changes = u->mmu->changes;
    if (ptr == NULL) {
        cpu->segfault_addr64 = 0;
        cpu->segfault_addr = (addr_t) address;
        cpu->segfault_was_write = write;
        u->interrupt = INT_GPF;
        return false;
    }

    if (is_mapped(u, page_addr)) {
        // Unicorn already has this page (read-only, typically copy-on-write)
        // and the page behind it has now been replaced or made writable.
        // Replacing a mapping from inside a protection hook is not safe in
        // unicorn, so stop, swap the mapping outside of emulation, and let
        // the instruction run again.
        u->remap_page = page_addr;
        u->remap_pending = true;
        u->stale_fault = true;
        u->interrupt = INT_TIMER;
        return false;
    }

    return map_run(u, mem, page_addr, ptr);
}

static void syscall_hook(uc_engine *uc, void *user_data) {
    struct ucpu *u = user_data;
    u->interrupt = INT_SYSCALL;
    uc_emu_stop(uc);
}

static void intr_hook(uc_engine *uc, uint32_t intno, void *user_data) {
    struct ucpu *u = user_data;
    if (u->stale_fault && (intno == 13 || intno == 14)) {
        u->stale_fault = false;
        u->interrupt = INT_TIMER;
        uc_emu_stop(uc);
        return;
    }
    switch (intno) {
        case 0: u->interrupt = INT_DIV; break;
        case 1: u->interrupt = INT_DEBUG; break;
        case 3: u->interrupt = INT_BREAKPOINT; break;
        case 6: u->interrupt = INT_UNDEFINED; break;
        default:
            fprintf(stderr, "unicorn64: cpu exception %u\n", intno);
            u->cpu->segfault_addr = 0;
            u->cpu->segfault_was_write = false;
            u->interrupt = INT_GPF;
            break;
    }
    uc_emu_stop(uc);
}

static void block_hook(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    (void) address; (void) size;
    struct ucpu *u = user_data;
    if (u->stepping)
        return;
    if (u->want_stop || __atomic_exchange_n(u->cpu->poked_ptr, false, __ATOMIC_SEQ_CST) || ++u->blocks >= 20000) {
        u->want_stop = false;
        u->blocks = 0;
        if (u->interrupt == INT_NONE)
            u->interrupt = INT_TIMER;
        uc_emu_stop(uc);
    }
}

static struct ucpu *ucpu_get(void) {
    pthread_once(&ucpu_once, ucpu_key_init);
    struct ucpu *u = pthread_getspecific(ucpu_key);
    if (u != NULL)
        return u;
    u = calloc(1, sizeof(*u));
    u->mapped_bits = calloc(MEM_PAGES / 8, 1);
    uc_must(uc_open(UC_ARCH_X86, UC_MODE_64, &u->uc));
    uc_hook h;
    uc_must(uc_hook_add(u->uc, &h,
                UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_FETCH_UNMAPPED |
                UC_HOOK_MEM_WRITE_PROT | UC_HOOK_MEM_READ_PROT | UC_HOOK_MEM_FETCH_PROT,
                mem_hook, u, 1, 0));
    uc_must(uc_hook_add(u->uc, &h, UC_HOOK_INSN, syscall_hook, u, 1, 0, UC_X86_INS_SYSCALL));
    uc_must(uc_hook_add(u->uc, &h, UC_HOOK_INTR, intr_hook, u, 1, 0));
    uc_must(uc_hook_add(u->uc, &h, UC_HOOK_BLOCK, block_hook, u, 1, 0));
    pthread_setspecific(ucpu_key, u);
    return u;
}

static const int gp_regs[] = {
    UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RBX,
    UC_X86_REG_RSP, UC_X86_REG_RBP, UC_X86_REG_RSI, UC_X86_REG_RDI,
    UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_R10, UC_X86_REG_R11,
    UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15,
};

static void regs_load(struct ucpu *u, struct cpu_state *cpu) {
    uc_engine *uc = u->uc;
    for (int i = 0; i < 16; i++)
        uc_must(uc_reg_write(uc, gp_regs[i], &cpu->regs[i]));
    uc_must(uc_reg_write(uc, UC_X86_REG_RIP, &cpu->rip));
    qword_t flags = cpu->eflags | 2;
    uc_must(uc_reg_write(uc, UC_X86_REG_EFLAGS, &flags));
    uc_must(uc_reg_write(uc, UC_X86_REG_FS_BASE, &cpu->fs_base));
    uc_must(uc_reg_write(uc, UC_X86_REG_GS_BASE, &cpu->gs_base));
    for (int i = 0; i < 16; i++)
        uc_must(uc_reg_write(uc, UC_X86_REG_XMM0 + i, &cpu->xmm[i]));
    uc_must(uc_reg_write(uc, UC_X86_REG_MXCSR, &cpu->mxcsr));
    uc_must(uc_reg_write(uc, UC_X86_REG_FPCW, &cpu->fcw));
}

static void regs_save(struct ucpu *u, struct cpu_state *cpu) {
    uc_engine *uc = u->uc;
    for (int i = 0; i < 16; i++)
        uc_must(uc_reg_read(uc, gp_regs[i], &cpu->regs[i]));
    uc_must(uc_reg_read(uc, UC_X86_REG_RIP, &cpu->rip));
    qword_t flags = 0;
    uc_must(uc_reg_read(uc, UC_X86_REG_EFLAGS, &flags));
    cpu->eflags = (dword_t) flags;
    // flags always live in eflags for this engine
    cpu->cf = cpu->cf_bit;
    cpu->of = cpu->of_bit;
    cpu->flags_res = 0;
    uc_must(uc_reg_read(uc, UC_X86_REG_FS_BASE, &cpu->fs_base));
    uc_must(uc_reg_read(uc, UC_X86_REG_GS_BASE, &cpu->gs_base));
    for (int i = 0; i < 16; i++)
        uc_must(uc_reg_read(uc, UC_X86_REG_XMM0 + i, &cpu->xmm[i]));
    uc_must(uc_reg_read(uc, UC_X86_REG_MXCSR, &cpu->mxcsr));
    uc_must(uc_reg_read(uc, UC_X86_REG_FPCW, &cpu->fcw));
}

// step: run exactly one instruction and return INT_NONE if it completed
// (used by the interpreter's lockstep check).
int unicorn64_run(struct cpu_state *cpu, bool step) {
    if (cpu->poked_ptr == NULL)
        cpu->poked_ptr = &cpu->_poked;
    // One guest thread executes at a time. Unicorn emulates locked
    // instructions (lock cmpxchg, xadd, ...) as separate load and store steps,
    // which is only atomic if no other instance touches the same memory
    // meanwhile; without this, threads of one process lose lock updates and
    // deadlock. The lock is dropped on every return to the kernel layer.
    static int serial_cfg = -1;
    if (serial_cfg < 0)
        serial_cfg = getenv("ISH_UC_PARALLEL") == NULL;
    bool serial = serial_cfg && !step;
    if (serial) pthread_mutex_lock(&serial_lock);
    mem_anon_hint = true;
    struct ucpu *u = ucpu_get();
    u->cpu = cpu;
    u->stepping = step;
    if (u->mmu != cpu->mmu) {
        // different address space (exec)
        unmap_all(u);
        u->mmu = cpu->mmu;
        u->changes = cpu->mmu->changes;
    } else if (u->region_count > UC_MAX_REGIONS) {
        // unicorn cannot hold more than 4096 regions, and gets slow well
        // before that. Start over and let the working set fault back in.
        unmap_all(u);
        u->changes = cpu->mmu->changes;
    } else if (u->changes != cpu->mmu->changes) {
        // the address space changed: drop only the pages that no longer match
        regions_validate(u);
        u->changes = cpu->mmu->changes;
    }

    if (!step && __atomic_exchange_n(cpu->poked_ptr, false, __ATOMIC_SEQ_CST)) {
        cpu->trapno = INT_TIMER;
        if (serial) pthread_mutex_unlock(&serial_lock);
        return INT_TIMER;
    }

    regs_load(u, cpu);
    u->interrupt = INT_NONE;
    uc_err err = uc_emu_start(u->uc, cpu->rip, 0, 0, (step || cpu->tf) ? 1 : 0);
    regs_save(u, cpu);
    if (u->remap_pending) {
        u->remap_pending = false;
        if (is_mapped(u, u->remap_page))
            forget_mapped(u, u->remap_page); // drops the whole region around it
    }

    int interrupt = u->interrupt;
    if (interrupt == INT_NONE) {
        if (err == UC_ERR_OK && step) {
            interrupt = INT_NONE;
        } else if (err == UC_ERR_OK && cpu->tf) {
            interrupt = INT_DEBUG;
        } else if (err == UC_ERR_INSN_INVALID) {
            interrupt = INT_UNDEFINED;
        } else {
            // hlt, a privileged instruction, or something unicorn gave up on
            if (err != UC_ERR_OK)
                fprintf(stderr, "unicorn64: stopped at %#llx: %s\n", (unsigned long long) cpu->rip, uc_strerror(err));
            cpu->segfault_addr = 0;
            cpu->segfault_was_write = false;
            interrupt = INT_GPF;
        }
    }
    if (!step)
        cpu->trapno = interrupt;
    if (stats_on < 0) {
        stats_on = getenv("ISH_UC_STATS") != NULL;
        if (getenv("ISH_UC_MAX_REGIONS"))
            max_regions = strtoul(getenv("ISH_UC_MAX_REGIONS"), NULL, 10);
    }
    if (stats_on) {
        static __thread unsigned long n_sys, n_timer, n_gpf, n_other, n_faults_last;
        static __thread time_t last;
        if (interrupt == INT_SYSCALL) n_sys++;
        else if (interrupt == INT_TIMER) n_timer++;
        else if (interrupt == INT_GPF) n_gpf++;
        else n_other++;
        time_t now = time(NULL);
        if (now >= last + 2) {
            last = now;
            fprintf(stderr, "uc64 stats: sys %lu timer %lu gpf %lu other %lu faults %lu(+%lu) pages %zu regions %zu rip %#llx changes %llu\n",
                    n_sys, n_timer, n_gpf, n_other, u->n_faults, u->n_faults - n_faults_last, u->pages_live, u->region_count,
                    (unsigned long long) cpu->rip, (unsigned long long) cpu->mmu->changes);
            n_faults_last = u->n_faults;
        }
    }
    if (serial) pthread_mutex_unlock(&serial_lock);
    return interrupt;
}
