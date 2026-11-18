#ifndef EMU_CPU_MEM_H
#define EMU_CPU_MEM_H

#include "misc.h"

// top 20 bits of an address, i.e. address >> 12
typedef dword_t page_t;
#define BAD_PAGE 0x10000

#ifndef __KERNEL__
#define PAGE_BITS 12
#undef PAGE_SIZE // defined in system headers somewhere
#define PAGE_SIZE (1 << PAGE_BITS)
#define PGOFFSET(addr) ((addr) & (PAGE_SIZE - 1))
typedef dword_t pages_t;
#if ISH_GUEST_64
// 2^31 pages = an 8 TB address space. Page numbers stay 32 bits wide; an
// address beyond the address space maps to page number MEM_PAGES, which is
// never mapped, so it faults like any other bad address.
#define MEM_PAGES (1u << 31)
static inline page_t mmu_page_of(qword_t addr) {
    qword_t page = addr >> PAGE_BITS;
    return page >= MEM_PAGES ? MEM_PAGES : (page_t) page;
}
#define PAGE(addr) mmu_page_of(addr)
#define PAGE_ROUND_UP(bytes) (mmu_page_of((qword_t) (bytes) + PAGE_SIZE - 1))
#define PAGE_ADDR(page) ((qword_t) (page) << PAGE_BITS)
#else
#define PAGE(addr) ((addr) >> PAGE_BITS)
// bytes MUST be unsigned if you would like this to overflow to zero
#define PAGE_ROUND_UP(bytes) (PAGE((bytes) + PAGE_SIZE - 1))
#define MEM_PAGES (1 << 20) // at least on 32-bit
#define PAGE_ADDR(page) ((page) << PAGE_BITS)
#endif
#endif

struct mmu {
    struct mmu_ops *ops;
    struct asbestos *asbestos;
    uint64_t changes;
};

#define MEM_READ 0
#define MEM_WRITE 1
#define MEM_WRITE_PTRACE 2

struct mmu_ops {
    // type is MEM_READ or MEM_WRITE
    void *(*translate)(struct mmu *mmu, addr_t addr, int type);
};

static inline void *mmu_translate(struct mmu *mmu, addr_t addr, int type) {
    return mmu->ops->translate(mmu, addr, type);
}

#endif