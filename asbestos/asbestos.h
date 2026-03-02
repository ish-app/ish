#ifndef ASBESTOS_H
#define ASBESTOS_H
#include "misc.h"
#include "emu/mmu.h"
#include "util/list.h"
#include "util/sync.h"

#define FIBER_INITIAL_HASH_SIZE (1 << 10)
#define FIBER_CACHE_SIZE (1 << 10)
#define FIBER_PAGE_HASH_SIZE (1 << 10)

struct asbestos {
    // there is one asbestos per address space
    struct mmu *mmu;
    size_t mem_used;
    size_t num_blocks;

    struct list *hash;
    size_t hash_size;

    // list of fiber_blocks that should be freed soon (at the next RCU grace
    // period, if we had such a thing)
    struct list jetsam;

    // A way to look up blocks in a page
    struct {
        struct list blocks[2];
    } *page_hash;

    lock_t lock;
    wrlock_t jetsam_lock;
};

// this is roughly the average number of instructions in a basic block according to anonymous sources
// times 4, roughly the average number of gadgets/parameters in an instruction, according to anonymous sources
#define FIBER_BLOCK_INITIAL_CAPACITY 16

struct fiber_block {
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
    // links for free list
    struct list jetsam;
    bool is_jetsam;

    unsigned long code[];
};

// Create a new asbestos
struct asbestos *asbestos_new(struct mmu *mmu);
void asbestos_free(struct asbestos *asbestos);

// Invalidate all fiber blocks in pages start (inclusive) to end (exclusive).
// Locks the asbestos. Should only be called by memory.c in conjunction with
// mem_changed.
void asbestos_invalidate_range(struct asbestos *asbestos, page_t start, page_t end);
void asbestos_invalidate_page(struct asbestos *asbestos, page_t page);
void asbestos_invalidate_all(struct asbestos *asbestos);

#endif
