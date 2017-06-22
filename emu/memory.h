#ifndef MEMORY_H
#define MEMORY_H

#include "misc.h"
#include <unistd.h>

// top 20 bits of an address, i.e. address >> 12
typedef dword_t page_t;
#define BAD_PAGE 0x10000

struct mem {
    struct pt_entry **pt;
    struct tlb_entry *tlb;
    page_t dirty_page;
};

// Initialize a mem struct
void mem_init(struct mem *mem);

#define PAGE_BITS 12
#define PAGE_SIZE (1 << PAGE_BITS)
#define PAGE(addr) ((addr) >> PAGE_BITS)
#define OFFSET(addr) ((addr) & (PAGE_SIZE - 1))
typedef dword_t pages_t;
#define PAGE_ROUND_UP(bytes) (((bytes - 1) / PAGE_SIZE) + 1)

#define BYTES_ROUND_DOWN(bytes) (PAGE(bytes) << PAGE_BITS)
#define BYTES_ROUND_UP(bytes) (PAGE_ROUND_UP(bytes) << PAGE_BITS)

struct pt_entry {
    void *data;
    unsigned refcount;
    unsigned flags;
    bits dirty:1;
};
#define PT_SIZE (1 << 20) // at least on 32-bit
// page flags
// P_READ and P_EXEC are ignored for now
#define P_READ (1 << 0)
#define P_WRITE (1 << 1)
#define P_EXEC (1 << 2)
#define P_GROWSDOWN (1 << 3)
#define P_GUARD (1 << 4)

page_t pt_find_hole(struct mem *mem, pages_t size);

// Map real memory into fake memory (unmaps existing mappings)
int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, unsigned flags);
// Map fake file into fake memory
int pt_map_file(struct mem *mem, page_t start, pages_t pages, int fd, off_t off, unsigned flags);
// Map empty space into fake memory
int pt_map_nothing(struct mem *mem, page_t page, pages_t pages, unsigned flags);
// Unmap fake memory, return -1 if any part of the range isn't mapped and 0 otherwise
int pt_unmap(struct mem *mem, page_t start, pages_t pages);
// Same as pt_unmap, but skips over unmapped parts
int pt_unmap_force(struct mem *mem, page_t start, pages_t pages);
// Set the flags on memory
int pt_set_flags(struct mem *mem, page_t start, pages_t pages, int flags);

void pt_dump(struct mem *mem);

struct tlb_entry {
    page_t page;
    page_t page_if_writable;
    uintptr_t data_minus_addr;
};
#define TLB_BITS 10
#define TLB_SIZE (1 << TLB_BITS)
#define TLB_INDEX(addr) ((addr >> PAGE_BITS) & (TLB_SIZE - 1))
#define TLB_READ 0
#define TLB_WRITE 1
#define TLB_PAGE(addr) (addr & 0xfffff000)
#define TLB_PAGE_EMPTY 1
void *tlb_handle_miss(struct mem *mem, addr_t addr, int type);

forceinline void *mem_read_ptr(struct mem *mem, addr_t addr) {
    struct tlb_entry entry = mem->tlb[TLB_INDEX(addr)];
    if (entry.page == TLB_PAGE(addr)) {
        void *address = (void *) (entry.data_minus_addr + addr);
        postulate(address != NULL);
        return address;
    }
    return tlb_handle_miss(mem, addr, TLB_READ);
}

forceinline void *mem_write_ptr(struct mem *mem, addr_t addr) {
    struct tlb_entry entry = mem->tlb[TLB_INDEX(addr)];
    if (entry.page_if_writable == TLB_PAGE(addr)) {
        mem->dirty_page = TLB_PAGE(addr);
        void *address = (void *) (entry.data_minus_addr + addr);
        postulate(address != NULL);
        return address;
    }
    return tlb_handle_miss(mem, addr, TLB_WRITE);
}

#endif
