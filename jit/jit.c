#define DEFAULT_CHANNEL instr
#include "debug.h"
#include "jit/jit.h"
#include "jit/gen.h"
#include "jit/frame.h"
#include "emu/cpu.h"
#include "emu/memory.h"
#include "emu/interrupt.h"
#include "util/list.h"

extern int current_pid(void);

static void jit_block_disconnect(struct jit *jit, struct jit_block *block);
static void jit_block_free(struct jit *jit, struct jit_block *block);
static void jit_free_jetsam(struct jit *jit);
static void jit_resize_hash(struct jit *jit, size_t new_size);

struct jit *jit_new(struct mmu *mmu) {
    struct jit *jit = calloc(1, sizeof(struct jit));
    jit->mmu = mmu;
    jit_resize_hash(jit, JIT_INITIAL_HASH_SIZE);
    jit->page_hash = calloc(JIT_PAGE_HASH_SIZE, sizeof(*jit->page_hash));
    list_init(&jit->jetsam);
    lock_init(&jit->lock);
    wrlock_init(&jit->jetsam_lock);
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
    jit_free_jetsam(jit);
    free(jit->page_hash);
    free(jit->hash);
    free(jit);
}

static inline struct list *blocks_list(struct jit *jit, page_t page, int i) {
    // TODO is this a good hash function?
    return &jit->page_hash[page % JIT_PAGE_HASH_SIZE].blocks[i];
}

void jit_invalidate_range(struct jit *jit, page_t start, page_t end) {
    lock(&jit->lock);
    struct jit_block *block, *tmp;
    for (page_t page = start; page < end; page++) {
        for (int i = 0; i <= 1; i++) {
            struct list *blocks = blocks_list(jit, page, i);
            if (list_null(blocks))
                continue;
            list_for_each_entry_safe(blocks, block, tmp, page[i]) {
                jit_block_disconnect(jit, block);
                block->is_jetsam = true;
                list_add(&jit->jetsam, &block->jetsam);
            }
        }
    }
    unlock(&jit->lock);
}

void jit_invalidate_page(struct jit *jit, page_t page) {
    jit_invalidate_range(jit, page, page + 1);
}
void jit_invalidate_all(struct jit *jit) {
    jit_invalidate_range(jit, 0, MEM_PAGES);
}

static void jit_resize_hash(struct jit *jit, size_t new_size) {
    TRACE_(verbose, "%d resizing hash to %lu, using %lu bytes for gadgets\n", current_pid(), new_size, jit->mem_used);
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
    TRACE("%d %08x --- compiling:\n", current_pid(), ip);
    gen_start(ip, &state);
    while (true) {
        if (!gen_step(&state, tlb))
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

// Remove all pointers to the block. It can't be freed yet because another
// thread may be executing it.
static void jit_block_disconnect(struct jit *jit, struct jit_block *block) {
    if (jit != NULL) {
        jit->mem_used -= block->used;
        jit->num_blocks--;
    }
    list_remove(&block->chain);
    for (int i = 0; i <= 1; i++) {
        list_remove(&block->page[i]);
        list_remove_safe(&block->jumps_from_links[i]);

        struct jit_block *prev_block, *tmp;
        list_for_each_entry_safe(&block->jumps_from[i], prev_block, tmp, jumps_from_links[i]) {
            if (prev_block->jump_ip[i] != NULL)
                *prev_block->jump_ip[i] = prev_block->old_jump_ip[i];
            list_remove(&prev_block->jumps_from_links[i]);
        }
    }
}

static void jit_block_free(struct jit *jit, struct jit_block *block) {
    jit_block_disconnect(jit, block);
    free(block);
}

static void jit_free_jetsam(struct jit *jit) {
    struct jit_block *block, *tmp;
    list_for_each_entry_safe(&jit->jetsam, block, tmp, jetsam) {
        list_remove(&block->jetsam);
        free(block);
    }
}

int jit_enter(struct jit_block *block, struct jit_frame *frame, struct tlb *tlb);

static inline size_t jit_cache_hash(addr_t ip) {
    return (ip ^ (ip >> 12)) % JIT_CACHE_SIZE;
}

static int cpu_step_to_interrupt(struct cpu_state *cpu, struct tlb *tlb) {
    struct jit *jit = cpu->mmu->jit;
    read_wrlock(&jit->jetsam_lock);

    struct jit_block **cache = calloc(JIT_CACHE_SIZE, sizeof(*cache));
    struct jit_frame *frame = malloc(sizeof(struct jit_frame));
    memset(frame, 0, sizeof(*frame));
    frame->cpu = *cpu;
    assert(jit->mmu == cpu->mmu);

    int interrupt = INT_NONE;
    while (interrupt == INT_NONE) {
        addr_t ip = frame->cpu.eip;
        size_t cache_index = jit_cache_hash(ip);
        struct jit_block *block = cache[cache_index];
        if (block == NULL || block->addr != ip) {
            lock(&jit->lock);
            block = jit_lookup(jit, ip);
            if (block == NULL) {
                block = jit_block_compile(ip, tlb);
                jit_insert(jit, block);
            } else {
                TRACE("%d %08x --- missed cache\n", current_pid(), ip);
            }
            cache[cache_index] = block;
            unlock(&jit->lock);
        }
        struct jit_block *last_block = frame->last_block;
        if (last_block != NULL &&
                (last_block->jump_ip[0] != NULL ||
                 last_block->jump_ip[1] != NULL)) {
            lock(&jit->lock);
            // can't mint new pointers to a block that has been marked jetsam
            // and is thus assumed to have no pointers left
            if (!last_block->is_jetsam && !block->is_jetsam) {
                for (int i = 0; i <= 1; i++) {
                    if (last_block->jump_ip[i] != NULL &&
                            (*last_block->jump_ip[i] & 0xffffffff) == block->addr) {
                        *last_block->jump_ip[i] = (unsigned long) block->code;
                        list_add(&block->jumps_from[i], &last_block->jumps_from_links[i]);
                    }
                }
            }

            unlock(&jit->lock);
        }
        frame->last_block = block;

        // block may be jetsam, but that's ok, because it can't be freed until
        // every thread on this jit is not executing anything

        TRACE("%d %08x --- cycle %ld\n", current_pid(), ip, frame->cpu.cycle);

        interrupt = jit_enter(block, frame, tlb);
        if (interrupt == INT_NONE && __atomic_exchange_n(cpu->poked_ptr, false, __ATOMIC_SEQ_CST))
            interrupt = INT_TIMER;
        if (interrupt == INT_NONE && ++frame->cpu.cycle % (1 << 10) == 0)
            interrupt = INT_TIMER;
        *cpu = frame->cpu;
    }

    free(frame);
    free(cache);
    read_wrunlock(&jit->jetsam_lock);
    return interrupt;
}

static int cpu_single_step(struct cpu_state *cpu, struct tlb *tlb) {
    struct gen_state state;
    gen_start(cpu->eip, &state);
    gen_step(&state, tlb);
    gen_exit(&state);
    gen_end(&state);

    struct jit_block *block = state.block;
    struct jit_frame frame = {.cpu = *cpu};
    int interrupt = jit_enter(block, &frame, tlb);
    *cpu = frame.cpu;
    jit_block_free(NULL, block);
    if (interrupt == INT_NONE)
        interrupt = INT_DEBUG;
    return interrupt;
}

int cpu_run_to_interrupt(struct cpu_state *cpu, struct tlb *tlb) {
    if (cpu->poked_ptr == NULL)
        cpu->poked_ptr = &cpu->_poked;
    tlb_refresh(tlb, cpu->mmu);
    int interrupt = (cpu->tf ? cpu_single_step : cpu_step_to_interrupt)(cpu, tlb);
    cpu->trapno = interrupt;

    struct jit *jit = cpu->mmu->jit;
    lock(&jit->lock);
    if (!list_empty(&jit->jetsam)) {
        // write-lock the jetsam_lock to wait until other jit threads get to
        // this point, so they will all clear out their block pointers
        // TODO: use RCU for better performance
        unlock(&jit->lock);
        write_wrlock(&jit->jetsam_lock);
        lock(&jit->lock);
        jit_free_jetsam(jit);
        write_wrunlock(&jit->jetsam_lock);
    }
    unlock(&jit->lock);

    return interrupt;
}

void cpu_poke(struct cpu_state *cpu) {
    __atomic_store_n(cpu->poked_ptr, true, __ATOMIC_SEQ_CST);
}
