#ifndef EMU_GEN_H
#define EMU_GEN_H

#include "emu/jit.h"
#include "emu/tlb.h"

struct gen_state {
    addr_t ip;
    struct jit_block *block;
    unsigned size;
    unsigned capacity;
};

void gen(struct gen_state *state, unsigned long thing);
void gen_step32(struct gen_state *state, struct tlb *tlb);
void gen_step16(struct gen_state *state, struct tlb *tlb);

#endif
