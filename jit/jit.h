#ifndef JIT_H
#define JIT_H
#include "misc.h"
#include "emu/memory.h"
#include "util/list.h"

#if JIT

#define JIT_INITIAL_HASH_SIZE (1 << 10)
#define JIT_CACHE_SIZE (1 << 10)

struct jit {
    // there is one jit per address space
    struct mem *mem;
    size_t mem_used;
    size_t num_blocks;
    struct list *hash;
    size_t hash_size;
    lock_t lock;
};

// this is roughly the average number of instructions in a basic block according to anonymous sources
// times 4, roughly the average number of gadgets/parameters in an instruction, according to anonymous sources
#define JIT_BLOCK_INITIAL_CAPACITY 16

struct jit_block {
    addr_t addr;
    addr_t end_addr;
    size_t used;

    // pointers to the ip values in the last gadget
    unsigned long *jump_ip[2];
    // original values of *jump_ip[]
    unsigned long old_jump_ip[2];
    // blocks that jump to this block
    struct list jumps_from[2];

    // hashtable bucket links
    struct list chain;
    // list of blocks in a page
    struct list page[2];
    // links for jumps_from
    struct list jumps_from_links[2];

    unsigned long code[];
};

// Create a new jit
struct jit *jit_new(struct mem *mem);
void jit_free(struct jit *jit);

// Invalidate all jit blocks in the given page. Locks the jit.
void jit_invalidate_page(struct jit *jit, page_t page);

#endif

#endif
