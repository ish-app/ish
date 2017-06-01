#ifndef MEMORY_H
#define MEMORY_H

#include "misc.h"
#include <unistd.h>

struct mem {
    struct pt_entry **pt;
    /* struct tlb_entry *tlb; */
};

// Initialize a mem struct
void mem_init(struct mem *mem);

#define MEM_READ(mem, addr, size) \
    (*((const UINT(size) *) &((char *) (mem)->pt[PAGE(addr)]->data)[OFFSET(addr)]))
#define MEM_WRITE(mem, addr, size) \
    (*((UINT(size) *) &((char *) (mem)->pt[PAGE(addr)]->data)[OFFSET(addr)]))

#define PAGE_BITS 12
#define PAGE_SIZE (1 << PAGE_BITS)
#define PAGE(addr) ((addr) >> PAGE_BITS)
#define OFFSET(addr) ((addr) & ~(UINT32_MAX << PAGE_BITS))
typedef dword_t pages_t;
#define PAGE_ROUND_UP(bytes) (((bytes - 1) / PAGE_SIZE) + 1)

struct pt_entry {
    void *data;
    unsigned refcount;
    unsigned flags;
    bits dirty:1;
};
#define PT_SIZE (1 << 20) // at least on 32-bit
// page flags
#define P_WRITABLE (1 << 0)

// Map real memory into fake memory (unmaps existing mappings)
int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, unsigned flags);
// Map fake file into fake memory
int pt_map_file(struct mem *mem, page_t start, pages_t pages, int fd, off_t off, unsigned flags);
// Map empty space into fake memory
int pt_map_nothing(struct mem *mem, page_t page, pages_t pages, unsigned flags);
// Unmap fake memory
void pt_unmap(struct mem *mem, page_t page, pages_t pages);

void pt_dump(struct mem *mem);

/* struct tlb_entry { */
/*     page_t page; */
/*     page_t page_if_writable; */
/*     void *data; */
/* }; */

#endif
