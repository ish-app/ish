#ifndef JIT_H
#define JIT_H
#include "emu/memory.h"
#include "util/list.h"

#define JIT_HASH_SIZE (1 << 10)

struct jit {
    // there is one jit per address space
    struct mem *mem;
    struct list hash[JIT_HASH_SIZE];
};

// this is roughly the average number of instructions in a basic block according to anonymous sources
// times 4, roughly the average number of gadgets/parameters in an instruction
#define JIT_BLOCK_INITIAL_CAPACITY 32

struct jit_block {
    struct list chain;
    addr_t addr;
    unsigned long code[];
};

// Create a new jit
struct jit *jit_new(struct mem *mem);
void jit_free(struct jit *jit);

#endif
