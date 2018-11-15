#ifndef MEMORY_H
#define MEMORY_H

#include <stdatomic.h>
#include <unistd.h>
#include <string.h>
#include "util/list.h"
#include "util/sync.h"
#include "misc.h"
#if JIT
struct jit;
#endif

// top 20 bits of an address, i.e. address >> 12
typedef dword_t page_t;
#define BAD_PAGE 0x10000

struct mem {
    atomic_uint refcount;
    atomic_uint changes; // increment whenever a tlb flush is needed
    struct pt_entry *pt; // TODO replace with red-black tree

    // TODO put these in their own mm struct maybe
    addr_t vdso; // immutable
    addr_t start_brk; // immutable
    addr_t brk;
#if JIT
    struct jit *jit;
#endif

    wrlock_t lock;
};
#define MEM_PAGES (1 << 20) // at least on 32-bit

// Create a new address space
struct mem *mem_new(void);
// Increment the refcount
void mem_retain(struct mem *mem);
// Decrement the refcount, destroy everything in the space if 0
void mem_release(struct mem *mem);

#define PAGE_BITS 12
#undef PAGE_SIZE // defined in system headers somewhere
#define PAGE_SIZE (1 << PAGE_BITS)
#define PAGE(addr) ((addr) >> PAGE_BITS)
#define PGOFFSET(addr) ((addr) & (PAGE_SIZE - 1))
typedef dword_t pages_t;
#define PAGE_ROUND_UP(bytes) (((bytes - 1) / PAGE_SIZE) + 1)

#define BYTES_ROUND_DOWN(bytes) (PAGE(bytes) << PAGE_BITS)
#define BYTES_ROUND_UP(bytes) (PAGE_ROUND_UP(bytes) << PAGE_BITS)

struct data {
    void *data; // immutable
    atomic_uint refcount;
};
struct pt_entry {
    struct data *data;
    size_t offset;
    unsigned flags;
#if JIT
    struct list blocks[2];
#endif
};
// page flags
// P_READ and P_EXEC are ignored for now
#define P_READ (1 << 0)
#define P_WRITE (1 << 1)
#undef P_EXEC // defined in sys/proc.h on darwin
#define P_EXEC (1 << 2)
#define P_GROWSDOWN (1 << 3)
#define P_COW (1 << 4)
#define P_WRITABLE(flags) (flags & P_WRITE && !(flags & P_COW))
#define P_COMPILED (1 << 5)
#define P_ANON (1 << 6)

bool pt_is_hole(struct mem *mem, page_t start, pages_t pages);
page_t pt_find_hole(struct mem *mem, pages_t size);

#define PT_FORCE 1

// Map real memory into fake memory (unmaps existing mappings). The memory is
// freed with munmap, so it must be allocated with mmap
int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, unsigned flags);
// Map fake file into fake memory
int pt_map_file(struct mem *mem, page_t start, pages_t pages, int fd, off_t off, unsigned flags);
// Map empty space into fake memory
int pt_map_nothing(struct mem *mem, page_t page, pages_t pages, unsigned flags);
// Unmap fake memory, return -1 if any part of the range isn't mapped and 0 otherwise
int pt_unmap(struct mem *mem, page_t start, pages_t pages, int force);
// Set the flags on memory
int pt_set_flags(struct mem *mem, page_t start, pages_t pages, int flags);
// Copy pages from src memory to dst memory using copy-on-write
int pt_copy_on_write(struct mem *src, page_t src_start, struct mem *dst, page_t dst_start, page_t pages);

#define MEM_READ 0
#define MEM_WRITE 1
void *mem_ptr(struct mem *mem, addr_t addr, int type);

extern size_t real_page_size;

#endif
