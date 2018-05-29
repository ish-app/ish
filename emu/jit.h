#ifndef JIT_H
#define JIT_H
#include "misc.h"
#include "emu/memory.h"
#include "util/list.h"

#if JIT

#define JIT_HASH_SIZE (1 << 10)

struct jit {
    // there is one jit per address space
    struct mem *mem;
    struct list hash[JIT_HASH_SIZE];
    lock_t lock;
};

// this is roughly the average number of instructions in a basic block according to anonymous sources
// times 4, roughly the average number of gadgets/parameters in an instruction, according to anonymous sources
#define JIT_BLOCK_INITIAL_CAPACITY 32

struct jit_block {
    struct list chain;
    struct list page[2]; // a block can span up to 2 pages
    addr_t addr;
    addr_t end_addr;
    unsigned long code[];
};

// Create a new jit
struct jit *jit_new(struct mem *mem);
void jit_free(struct jit *jit);

// Invalidate all jit blocks in the given page. Locks the jit.
void jit_invalidate_page(struct jit *jit, page_t page);

#else

static inline void jit_invalidate_page(struct jit *jit, page_t page) {}

#endif

#endif
