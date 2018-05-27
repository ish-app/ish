#include "util/list.h"
#include "emu/cpu.h"
#include "emu/memory.h"
#include "emu/jit.h"
#include "emu/interrupt.h"
#include "emu/gen.h"
#include "kernel/calls.h"

struct jit *jit_new(struct mem *mem) {
    struct jit *jit = malloc(sizeof(struct jit));
    jit->mem = mem;
    for (int i = 0; i < JIT_HASH_SIZE; i++)
        list_init(&jit->hash[i]);
    return jit;
}

void jit_free(struct jit *jit) {
    for (int i = 0; i < JIT_HASH_SIZE; i++) {
        struct jit_block *block, *tmp;
        list_for_each_entry_safe(&jit->hash[i], block, tmp, chain) {
            list_remove(&block->chain);
            free(block);
        }
    }
    free(jit);
}

void jit_insert(struct jit *jit, struct jit_block *block) {
    list_add(&jit->hash[block->addr % JIT_HASH_SIZE], &block->chain);
}

struct jit_block *jit_lookup(struct jit *jit, addr_t addr) {
    struct jit_block *block;
    list_for_each_entry(&jit->hash[addr % JIT_HASH_SIZE], block, chain) {
        if (block->addr == addr)
            return block;
    }
    return NULL;
}

static void jit_block_free(struct jit_block *block) {
    free(block);
}

struct jit_block *jit_block_compile(addr_t ip, struct tlb *tlb) {
    struct gen_state state;
    gen_start(ip, &state);
    bool in_block = true;
    while (in_block)
        in_block = gen_step32(&state, tlb);
    return state.block;
}

// assembler function
int jit_enter(struct jit_block *block, struct cpu_state *cpu, struct tlb *tlb);

void cpu_run(struct cpu_state *cpu) {
    struct tlb *tlb = tlb_new(cpu->mem);
    struct jit *jit = jit_new(cpu->mem);

    int i = 0;
    read_wrlock(&cpu->mem->lock);
    int changes = cpu->mem->changes;

    // look to see if we have a matching block
    // if so, (link it into the previous block and) run it
    // otherwise, make a new block and stick it in the hash

    while (true) {
        struct jit_block *block = jit_lookup(jit, cpu->eip);
        if (block == NULL) {
            block = jit_block_compile(cpu->eip, tlb);
            jit_insert(jit, block);
        }
        int interrupt = jit_enter(block, cpu, tlb);
        if (interrupt == INT_NONE && i++ >= 100000) {
            i = 0;
            interrupt = INT_TIMER;
        }
        if (interrupt != INT_NONE) {
            cpu->trapno = interrupt;
            read_wrunlock(&cpu->mem->lock);
            handle_interrupt(interrupt);
            read_wrlock(&cpu->mem->lock);
            if (tlb->mem != cpu->mem)
                tlb->mem = cpu->mem;
            if (cpu->mem->changes != changes) {
                tlb_flush(tlb);
                changes = cpu->mem->changes;
            }
        }
    }
}

// really only here for ptraceomatic
int cpu_step32(struct cpu_state *cpu, struct tlb *tlb) {
    struct gen_state state;
    gen_start(cpu->eip, &state);
    gen_step32(&state, tlb);
    gen_exit(&state);

    struct jit_block *block = state.block;
    int interrupt = jit_enter(block, cpu, tlb);
    jit_block_free(block);
    return interrupt;
}
