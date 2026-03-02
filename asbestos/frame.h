#include <stdatomic.h>
#include "emu/cpu.h"

// keep in sync with asm
#define FIBER_RETURN_CACHE_SIZE 4096
#define FIBER_RETURN_CACHE_HASH(x) ((x) & 0xFFF0) >> 4)

struct fiber_frame {
    struct cpu_state cpu;
    void *bp;
    addr_t value_addr;
    uint64_t value[2]; // buffer for crosspage crap
    struct fiber_block *last_block;
    long ret_cache[FIBER_RETURN_CACHE_SIZE]; // a map of ip to pointer-to-call-gadget-arguments
};
