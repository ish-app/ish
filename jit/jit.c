#define DEFAULT_CHANNEL instr
#include "debug.h"
#include "jit/jit.h"
#include "jit/gen.h"
#include "jit/frame.h"
#include "emu/cpu.h"
#include "emu/memory.h"
#include "emu/interrupt.h"
#include "util/list.h"
#include "kernel/calls.h"

static void jit_block_free(struct jit *jit, struct jit_block *block);
static void jit_resize_hash(struct jit *jit, size_t new_size);

struct jit *jit_new(struct mem *mem) {
    struct jit *jit = calloc(1, sizeof(struct jit));
    jit->mem = mem;
    jit_resize_hash(jit, JIT_INITIAL_HASH_SIZE);
    lock_init(&jit->lock);
    return jit;
}

void jit_free(struct jit *jit) {
    for (size_t i = 0; i < jit->hash_size; i++) {
        struct jit_block *block, *tmp;
        if (list_null(&jit->hash[i]))
            continue;
        list_for_each_entry_safe(&jit->hash[i], block, tmp, chain) {
            jit_block_free(jit, block);
        }
    }
    free(jit->hash);
    free(jit);
}

static inline struct list *blocks_list(struct jit *jit, page_t page, int i) {
    return &mem_pt(jit->mem, page)->blocks[i];
}

void jit_invalidate_page(struct jit *jit, page_t page) {
    lock(&jit->lock);
    struct jit_block *block, *tmp;
    for (int i = 0; i <= 1; i++) {
        struct list *blocks = blocks_list(jit, page, i);
        if (list_null(blocks))
            continue;
        list_for_each_entry_safe(blocks, block, tmp, page[i]) {
            jit_block_free(jit, block);
        }
    }
    unlock(&jit->lock);
}

static void jit_resize_hash(struct jit *jit, size_t new_size) {
    TRACE_(verbose, "%d resizing hash to %lu, using %lu bytes for gadgets\n", current ? current->pid : 0, new_size, jit->mem_used);
    struct list *new_hash = calloc(new_size, sizeof(struct list));
    for (size_t i = 0; i < jit->hash_size; i++) {
        if (list_null(&jit->hash[i]))
            continue;
        struct jit_block *block, *tmp;
        list_for_each_entry_safe(&jit->hash[i], block, tmp, chain) {
            list_remove(&block->chain);
            list_init_add(&new_hash[block->addr % new_size], &block->chain);
        }
    }
    free(jit->hash);
    jit->hash = new_hash;
    jit->hash_size = new_size;
}

static void jit_insert(struct jit *jit, struct jit_block *block) {
    jit->mem_used += block->used;
    jit->num_blocks++;
    // target an average hash chain length of 1-2
    if (jit->num_blocks >= jit->hash_size * 2)
        jit_resize_hash(jit, jit->hash_size * 2);

    list_init_add(&jit->hash[block->addr % jit->hash_size], &block->chain);
    if (mem_pt(jit->mem, PAGE(block->addr)) == NULL)
        return;
    list_init_add(blocks_list(jit, PAGE(block->addr), 0), &block->page[0]);
    if (PAGE(block->addr) != PAGE(block->end_addr))
        list_init_add(blocks_list(jit, PAGE(block->end_addr), 1), &block->page[1]);
}

static struct jit_block *jit_lookup(struct jit *jit, addr_t addr) {
    struct list *bucket = &jit->hash[addr % jit->hash_size];
    if (list_null(bucket))
        return NULL;
    struct jit_block *block;
    list_for_each_entry(bucket, block, chain) {
        if (block->addr == addr)
            return block;
    }
    return NULL;
}

static struct jit_block *jit_block_compile(addr_t ip, struct tlb *tlb) {
    struct gen_state state;
    TRACE("%d %08x --- compiling:\n", current->pid, ip);
    gen_start(ip, &state);
    while (true) {
        if (!gen_step32(&state, tlb))
            break;
        // no block should span more than 2 pages
        // guarantee this by limiting total block size to 1 page
        // guarantee that by stopping as soon as there's less space left than
        // the maximum length of an x86 instruction
        // TODO refuse to decode instructions longer than 15 bytes
        if (state.ip - ip >= PAGE_SIZE - 15) {
            gen_exit(&state);
            break;
        }
    }
    gen_end(&state);
    assert(state.ip - ip <= PAGE_SIZE);
    state.block->used = state.capacity;
    return state.block;
}

static void jit_block_free(struct jit *jit, struct jit_block *block) {
    if (jit != NULL) {
        jit->mem_used -= block->used;
        jit->num_blocks--;
    }
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

int jit_enter(struct jit_block *block, struct jit_frame *frame, struct tlb *tlb);

#if 1

static inline size_t jit_cache_hash(addr_t ip) {
    return (ip ^ (ip >> 12)) % JIT_CACHE_SIZE;
}

void cpu_run(struct cpu_state *cpu) {
    struct tlb tlb;
    tlb_init(&tlb, cpu->mem);
    struct jit *jit = cpu->mem->jit;
    struct jit_block *cache[JIT_CACHE_SIZE] = {};
    struct jit_frame frame = {.cpu = *cpu};

    int i = 0;
    read_wrlock(&cpu->mem->lock);
    unsigned changes = cpu->mem->changes;

    while (true) {
        addr_t ip = frame.cpu.eip;
        size_t cache_index = jit_cache_hash(ip);
        struct jit_block *block = cache[cache_index];
        if (block == NULL || block->addr != ip) {
            lock(&jit->lock);
            block = jit_lookup(jit, ip);
            if (block == NULL) {
                block = jit_block_compile(ip, &tlb);
                jit_insert(jit, block);
            } else {
                TRACE("%d %08x --- missed cache\n", current->pid, ip);
            }
            cache[cache_index] = block;
            unlock(&jit->lock);
        }
        struct jit_block *last_block = frame.last_block;
        if (last_block != NULL &&
                (last_block->jump_ip[0] != NULL ||
                 last_block->jump_ip[1] != NULL)) {
            lock(&jit->lock);
            for (int i = 0; i <= 1; i++) {
                if (last_block->jump_ip[i] != NULL &&
                        (*last_block->jump_ip[i] & 0xffffffff) == block->addr) {
                    *last_block->jump_ip[i] = (unsigned long) block->code;
                    list_add(&block->jumps_from[i], &last_block->jumps_from_links[i]);
                }
            }
            unlock(&jit->lock);
        }
        frame.last_block = block;

        TRACE("%d %08x --- cycle %d\n", current->pid, ip, i);
        int interrupt = jit_enter(block, &frame, &tlb);
        if (interrupt == INT_NONE && ++i % (1 << 10) == 0)
            interrupt = INT_TIMER;
        if (interrupt != INT_NONE) {
            *cpu = frame.cpu;
            cpu->trapno = interrupt;
            read_wrunlock(&cpu->mem->lock);
            handle_interrupt(interrupt);
            read_wrlock(&cpu->mem->lock);

            jit = cpu->mem->jit;
            last_block = NULL;
            tlb.mem = cpu->mem;
            if (cpu->mem->changes != changes) {
                tlb_flush(&tlb);
                changes = cpu->mem->changes;
            }
            memset(cache, 0, sizeof(cache));
            frame.cpu = *cpu;
            frame.last_block = NULL;
        }
    }
}

#else

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

#endif

// really only here for ptraceomatic
int cpu_step32(struct cpu_state *cpu, struct tlb *tlb) {
    struct gen_state state;
    gen_start(cpu->eip, &state);
    gen_step32(&state, tlb);
    gen_exit(&state);
    gen_end(&state);

    struct jit_block *block = state.block;
    struct jit_frame frame = {.cpu = *cpu};
    int interrupt = jit_enter(block, &frame, tlb);
    *cpu = frame.cpu;
    jit_block_free(NULL, block);
    return interrupt;
}
