#ifndef THREADING_H
#define THREADING_H
#include "misc.h"
#include "emu/mmu.h"
#include "util/list.h"
#include "util/sync.h"

#define THREADED_INITIAL_HASH_SIZE (1 << 10)
#define THREADED_CACHE_SIZE (1 << 10)
#define THREADED_PAGE_HASH_SIZE (1 << 10)

struct weave {
    // there is one weave per address space
    struct mmu *mmu;
    size_t mem_used;
    size_t num_blocks;

    struct list *hash;
    size_t hash_size;

    // list of threaded_blocks that should be freed soon (at the next RCU grace
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
#define THREADED_BLOCK_INITIAL_CAPACITY 16

struct threaded_block {
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

// Create a new weave
struct weave *weave_new(struct mmu *mmu);
void weave_free(struct weave *weave);

// Invalidate all threaded blocks in pages start (inclusive) to end (exclusive).
// Locks the weave. Should only be called by memory.c in conjunction with
// mem_changed.
void weave_invalidate_range(struct weave *weave, page_t start, page_t end);
void weave_invalidate_page(struct weave *weave, page_t page);
void weave_invalidate_all(struct weave *weave);

#endif
