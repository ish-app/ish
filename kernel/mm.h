#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include "emu/memory.h"
#include "misc.h"

// uses mem.lock instead of having a lock of its own
struct mm {
    atomic_uint refcount;
    struct mem mem;

    addr_t vdso; // immutable
    addr_t start_brk; // immutable
    addr_t brk;
};

// Create a new address space
struct mm *mm_new(void);
// Increment the refcount
void mm_retain(struct mm *mem);
// Decrement the refcount, destroy everything in the space if 0
void mm_release(struct mm *mem);

#endif
