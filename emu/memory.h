#ifndef MEMORY_H
#define MEMORY_H

#include "emu/types.h"
#include <unistd.h>

#define PAGE_BITS 12
typedef uint8_t page[1 << PAGE_BITS];
#define PAGE_ADDR(addr) (addr >> PAGE_BITS)
#define OFFSET_ADDR(addr) (addr & ~(UINT32_MAX << PAGE_BITS))

// flags
#define P_WRITABLE (1 << 1)

struct pt_entry {
    page *data;
    unsigned refcount;
    unsigned flags;
};

#define PT_SIZE (1 << 20) // at least on 32-bit
typedef struct pt_entry **pagetable;
pagetable pt_alloc();

typedef word_t pages_t;
#define PAGES_FROM_BYTES(bytes) (((bytes - 1) / sizeof(page)) + 1)

// Map real memory into fake memory (unmaps existing mappings)
int pt_map(pagetable pt, page_t start, pages_t pages, page *memory, unsigned flags);
// Map fake file into fake memory
int pt_map_file(pagetable pt, page_t start, pages_t pages, int fd, off_t off, unsigned flags);
// Map empty space into fake memory
int pt_map_nothing(pagetable pt, page_t page, pages_t pages, unsigned flags);
// Unmap fake memory
void pt_unmap(pagetable pt, page_t page, pages_t pages);

void pt_dump(pagetable pt);

#endif
