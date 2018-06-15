#ifndef EMU_GEN_H
#define EMU_GEN_H

#include "jit/jit.h"
#include "emu/tlb.h"

struct gen_state {
    addr_t ip;
    struct jit_block *block;
    unsigned size;
    unsigned capacity;
    unsigned jump_ip[2];
};

void gen_start(addr_t addr, struct gen_state *state);
void gen_exit(struct gen_state *state);
void gen_end(struct gen_state *state);

int gen_step32(struct gen_state *state, struct tlb *tlb);
int gen_step16(struct gen_state *state, struct tlb *tlb);

#endif
