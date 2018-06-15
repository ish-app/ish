#include "jit/jit.h"
#include "jit/gen.h"
#include "jit/frame.h"
#include "emu/cpu.h"
#include "emu/memory.h"
#include "emu/interrupt.h"
#include "util/list.h"
#include "kernel/calls.h"

static void jit_block_free(struct jit_block *block);

struct jit *jit_new(struct mem *mem) {
    struct jit *jit = malloc(sizeof(struct jit));
    jit->mem = mem;
    for (int i = 0; i < JIT_HASH_SIZE; i++)
        list_init(&jit->hash[i]);
    lock_init(&jit->lock);
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

static inline struct list *blocks_list(struct jit *jit, page_t page, int i) {
    return &jit->mem->pt[page].blocks[i];
}

void jit_invalidate_page(struct jit *jit, page_t page) {
    lock(&jit->lock);
    struct jit_block *block, *tmp;
    for (int i = 0; i <= 1; i++) {
        struct list *blocks = blocks_list(jit, page, i);
        if (list_null(blocks))
            continue;
        list_for_each_entry_safe(blocks, block, tmp, page[i]) {
            jit_block_free(block);
        }
    }
    unlock(&jit->lock);
}

static void jit_insert(struct jit *jit, struct jit_block *block) {
    list_add(&jit->hash[block->addr % JIT_HASH_SIZE], &block->chain);
    list_init_add(blocks_list(jit, PAGE(block->addr), 0), &block->page[0]);
    if (PAGE(block->addr) != PAGE(block->end_addr))
        list_init_add(blocks_list(jit, PAGE(block->end_addr), 1), &block->page[1]);
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
        // no block should span more than 2 pages
        // guarantee this by limiting total block size to 1 page
        // guarantee that by stopping as soon as there's less space left than
        // the maximum length of an x86 instruction
        // TODO refuse to decode instructions longer than 15 bytes
        if (state.ip - ip >= PAGE_SIZE - 15)
            break;
    }
    gen_end(&state);
    assert(state.ip - ip <= PAGE_SIZE);
    return state.block;
}

static void jit_block_free(struct jit_block *block) {
    list_remove(&block->chain);
    for (int i = 0; i <= 1; i++) {
        list_remove(&block->page[i]);
        list_remove_safe(&block->jumps_from_links[i]);

        struct jit_block *last_block, *tmp;
        list_for_each_entry_safe(&block->jumps_from[i], last_block, tmp, jumps_from_links[i]) {
            if (last_block->jump_ip[i] != NULL)
                *last_block->jump_ip[i] = last_block->old_jump_ip[i];
            list_remove(&last_block->jumps_from_links[i]);
        }
    }
    free(block);
}

static inline int jit_enter_cpu(struct jit_block *block, struct cpu_state *cpu, struct tlb *tlb) {
    extern int jit_enter(struct jit_block *block, struct jit_frame *frame, struct tlb *tlb);

    struct jit_frame frame = {
        .cpu = *cpu,
    };
    int interrupt = jit_enter(block, &frame, tlb);
    *cpu = frame.cpu;
    return interrupt;
}

void cpu_run(struct cpu_state *cpu) {
    struct tlb *tlb = tlb_new(cpu->mem);
    struct jit *jit = cpu->mem->jit;

    int i = 0;
    read_wrlock(&cpu->mem->lock);
    int changes = cpu->mem->changes;

    struct jit_block *last_block = NULL;

    while (true) {
        lock(&jit->lock);
        struct jit_block *block = jit_lookup(jit, cpu->eip);
        if (block == NULL) {
            block = jit_block_compile(cpu->eip, tlb);
            jit_insert(jit, block);
        }
        if (last_block != NULL) {
            for (int i = 0; i <= 1; i++) {
                if (last_block->jump_ip[i] != NULL &&
                        (*last_block->jump_ip[i] & 0xffffffff) == block->addr) {
                    *last_block->jump_ip[i] = (unsigned long) block->code;
                    list_add(&block->jumps_from[i], &last_block->jumps_from_links[i]);
                }
            }
        }
        unlock(&jit->lock);
        last_block = block;

        int interrupt = jit_enter_cpu(block, cpu, tlb);
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
    gen_end(&state);

    struct jit_block *block = state.block;
    int interrupt = jit_enter_cpu(block, cpu, tlb);
    jit_block_free(block);
    return interrupt;
}
