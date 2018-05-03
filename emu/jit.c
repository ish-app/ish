#include "util/list.h"
#include "emu/cpu.h"
#include "emu/memory.h"
#include "emu/jit.h"
#include "emu/gadgets.h"

static void jit_block_free(struct jit_block *block) {
    free(block);
}

struct gen_state {
    struct jit_block *block;
    unsigned size;
    unsigned capacity;
};

void gen_start(addr_t addr, struct gen_state *state) {
    state->capacity = JIT_BLOCK_INITIAL_CAPACITY;
    state->size = 0;
    state->block = malloc(sizeof(struct jit_block) + state->capacity * sizeof(unsigned long));
}

static void gen(struct gen_state *state, unsigned long thing) {
    assert(state->size <= state->capacity);
    if (state->size >= state->capacity) {
        state->capacity *= 2;
        struct jit_block *bigger_block = malloc(sizeof(struct jit_block) +
                state->capacity * sizeof(unsigned long));
        if (bigger_block == NULL) {
            println("out of memory while jitting");
            abort();
        }
        state->block = bigger_block;
    }
    assert(state->size < state->capacity);
    state->block->code[state->size++] = thing;
}

#define GEN(thing) gen(state, (unsigned long) (thing))
#define G(g) GEN(gadget_##g)
#define GG(g, a) GEN(gadget_##g); GEN(a)

#include "emu/gen.c"

static struct jit_block *jit_compile_step(struct jit *jit, struct tlb *tlb, addr_t ip) {
    struct gen_state state;
    gen_start(ip, &state);
    gen_step32(&state, ip, tlb);
    gen(&state, (unsigned long) gadget_exit); // in case the last instruction didn't end the block
    return state.block;
}

int cpu_step32(struct cpu_state *cpu, struct tlb *tlb) {
    // assembler function
    extern int jit_enter(struct cpu_state *cpu, struct jit_block *block);

    struct jit_block *block = jit_compile_step(cpu->jit, tlb, cpu->eip);
    int interrupt = jit_enter(cpu, block);
    jit_block_free(block);
    return interrupt;
}

// TODO rewrite
void cpu_run(struct cpu_state *cpu) {
    int i = 0;
    struct tlb tlb = {.mem = cpu->mem};
    tlb_flush(&tlb);
    read_wrlock(&cpu->mem->lock);
    int changes = cpu->mem->changes;
    while (true) {
        int interrupt = cpu_step32(cpu, &tlb);
        if (interrupt == INT_NONE && i++ >= 100000) {
            i = 0;
            interrupt = INT_TIMER;
        }
        if (interrupt != INT_NONE) {
            cpu->trapno = interrupt;
            read_wrunlock(&cpu->mem->lock);
            handle_interrupt(interrupt);
            read_wrlock(&cpu->mem->lock);
            if (tlb.mem != cpu->mem)
                tlb.mem = cpu->mem;
            if (cpu->mem->changes != changes) {
                tlb_flush(&tlb);
                changes = cpu->mem->changes;
            }
        }
    }
}

// these functions are never used, but will be someday
struct jit *jit_new(struct mem *mem) {
    struct jit *jit = malloc(sizeof(struct jit));
    return jit;
}
void jit_free(struct jit *jit) {
    free(jit);
}

