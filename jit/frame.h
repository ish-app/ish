#include "emu/cpu.h"

struct jit_frame {
    struct cpu_state cpu;
    void *bp;
    uint32_t value; // buffer for crosspage crap
    addr_t value_addr;
};
