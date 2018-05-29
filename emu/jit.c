#include "emu/cpu.h"
#include "emu/memory.h"
#include "emu/jit.h"
#include "emu/interrupt.h"
#include "emu/gen.h"
#include "util/list.h"
#include "kernel/calls.h"

static void jit_block_free(struct jit_block *block);

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
            jit_block_free(block);
        }
    }
    free(jit);
}

void jit_invalidate_page(struct jit *jit, page_t page) {
    lock(&jit->lock);
    struct jit_block *block, *tmp;
    list_for_each_entry_safe(&jit->mem->pt[page].blocks[0], block, tmp, page[0]) {
        jit_block_free(block);
    }
    list_for_each_entry_safe(&jit->mem->pt[page].blocks[1], block, tmp, page[1]) {
        jit_block_free(block);
    }
    unlock(&jit->lock);
}

static void jit_insert(struct jit *jit, struct jit_block *block) {
    list_add(&jit->hash[block->addr % JIT_HASH_SIZE], &block->chain);
    list_add(&jit->mem->pt[PAGE(block->addr)].blocks[0], &block->page[0]);
    if (PAGE(block->addr) != PAGE(block->end_addr))
        list_add(&jit->mem->pt[PAGE(block->end_addr)].blocks[0], &block->page[1]);
}

static struct jit_block *jit_lookup(struct jit *jit, addr_t addr) {
    struct jit_block *block;
    list_for_each_entry(&jit->hash[addr % JIT_HASH_SIZE], block, chain) {
        if (block->addr == addr)
            return block;
    }
    return NULL;
}

static struct jit_block *jit_block_compile(addr_t ip, struct tlb *tlb) {
    struct gen_state state;
    gen_start(ip, &state);
    while (true) {
        if (!gen_step32(&state, tlb))
            break;
        // no block should span more than 2 pages, guarantee this by stopping
        // as soon as there's less space left than the maximum length of an
        // x86 instruction
        // TODO refuse to decode instructions longer than 15 bytes
        if (state.ip - ip >= PAGE_SIZE - 15)
            break;
    }
    return state.block;
}

static void jit_block_free(struct jit_block *block) {
    list_remove(&block->chain);
    list_remove(&block->page[0]);
    if (!list_empty(&block->page[1]))
        list_remove(&block->page[1]);
    free(block);
}

// assembler function
int jit_enter(struct jit_block *block, struct cpu_state *cpu, struct tlb *tlb);

void cpu_run(struct cpu_state *cpu) {
    struct tlb *tlb = tlb_new(cpu->mem);
    struct jit *jit = cpu->mem->jit;

    int i = 0;
    read_wrlock(&cpu->mem->lock);
    int changes = cpu->mem->changes;

    // TODO make a blockchain, raise $5M

    while (true) {
        lock(&jit->lock);
        struct jit_block *block = jit_lookup(jit, cpu->eip);
        if (block == NULL) {
            block = jit_block_compile(cpu->eip, tlb);
            jit_insert(jit, block);
        }
        unlock(&jit->lock);
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
