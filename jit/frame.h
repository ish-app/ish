#include "emu/cpu.h"

struct jit_frame {
    struct cpu_state cpu;
    void *bp;
    addr_t value_addr;
    uint64_t value[2]; // buffer for crosspage crap
    struct jit_block *last_block;
};
