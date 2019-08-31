#include "emu/cpu.h"

// keep in sync with asm
#define JIT_RETURN_CACHE_SIZE 65536
#define JIT_RETURN_CACHE_HASH(x) ((x) & 0xFFFF)

struct jit_frame {
    struct cpu_state cpu;
    void *bp;
    addr_t value_addr;
    uint64_t value[2]; // buffer for crosspage crap
    struct jit_block *last_block;
    long ret_cache[JIT_RETURN_CACHE_SIZE];
};
