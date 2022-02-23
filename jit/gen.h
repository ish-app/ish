#ifndef EMU_GEN_H
#define EMU_GEN_H

#include "jit/jit.h"
#include "emu/tlb.h"

struct gen_state {
    addr_t ip;
    addr_t orig_ip;
    unsigned long orig_ip_extra;
    struct jit_block *block;
    unsigned size;
    unsigned capacity;
    unsigned jump_ip[2];
    unsigned block_patch_ip; // for call/call_indir gadgets
};

void gen_start(addr_t addr, struct gen_state *state);
void gen_exit(struct gen_state *state);
void gen_end(struct gen_state *state);

int gen_step(struct gen_state *state, struct tlb *tlb);

#endif
