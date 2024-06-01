#define DEFAULT_CHANNEL instr
#include "debug.h"
#include "interpreter/weave.h"
#include "interpreter/gen.h"
#include "interpreter/frame.h"
#include "emu/cpu.h"
#include "emu/memory.h"
#include "emu/interrupt.h"
#include "util/list.h"

extern int current_pid(void);

static void threaded_block_disconnect(struct weave *weave, struct threaded_block *block);
static void threaded_block_free(struct weave *weave, struct threaded_block *block);
static void threaded_free_jetsam(struct weave *weave);
static void threaded_resize_hash(struct weave *weave, size_t new_size);

struct weave *weave_new(struct mmu *mmu) {
    struct weave *weave = calloc(1, sizeof(struct weave));
    weave->mmu = mmu;
    threaded_resize_hash(weave, THREADED_INITIAL_HASH_SIZE);
    weave->page_hash = calloc(THREADED_PAGE_HASH_SIZE, sizeof(*weave->page_hash));
    list_init(&weave->jetsam);
    lock_init(&weave->lock);
    wrlock_init(&weave->jetsam_lock);
    return weave;
}

void weave_free(struct weave *weave) {
    for (size_t i = 0; i < weave->hash_size; i++) {
        struct threaded_block *block, *tmp;
        if (list_null(&weave->hash[i]))
            continue;
        list_for_each_entry_safe(&weave->hash[i], block, tmp, chain) {
            threaded_block_free(weave, block);
        }
    }
    threaded_free_jetsam(weave);
    free(weave->page_hash);
    free(weave->hash);
    free(weave);
}

static inline struct list *blocks_list(struct weave *weave, page_t page, int i) {
    // TODO is this a good hash function?
    return &weave->page_hash[page % THREADED_PAGE_HASH_SIZE].blocks[i];
}

void weave_invalidate_range(struct weave *weave, page_t start, page_t end) {
    lock(&weave->lock);
    struct threaded_block *block, *tmp;
    for (page_t page = start; page < end; page++) {
        for (int i = 0; i <= 1; i++) {
            struct list *blocks = blocks_list(weave, page, i);
            if (list_null(blocks))
                continue;
            list_for_each_entry_safe(blocks, block, tmp, page[i]) {
                threaded_block_disconnect(weave, block);
                block->is_jetsam = true;
                list_add(&weave->jetsam, &block->jetsam);
            }
        }
    }
    unlock(&weave->lock);
}

void weave_invalidate_page(struct weave *weave, page_t page) {
    weave_invalidate_range(weave, page, page + 1);
}
void weave_invalidate_all(struct weave *weave) {
    weave_invalidate_range(weave, 0, MEM_PAGES);
}

static void threaded_resize_hash(struct weave *weave, size_t new_size) {
    TRACE_(verbose, "%d resizing hash to %lu, using %lu bytes for gadgets\n", current_pid(), new_size, weave->mem_used);
    struct list *new_hash = calloc(new_size, sizeof(struct list));
    for (size_t i = 0; i < weave->hash_size; i++) {
        if (list_null(&weave->hash[i]))
            continue;
        struct threaded_block *block, *tmp;
        list_for_each_entry_safe(&weave->hash[i], block, tmp, chain) {
            list_remove(&block->chain);
            list_init_add(&new_hash[block->addr % new_size], &block->chain);
        }
    }
    free(weave->hash);
    weave->hash = new_hash;
    weave->hash_size = new_size;
}

static void threaded_insert(struct weave *weave, struct threaded_block *block) {
    weave->mem_used += block->used;
    weave->num_blocks++;
    // target an average hash chain length of 1-2
    if (weave->num_blocks >= weave->hash_size * 2)
        threaded_resize_hash(weave, weave->hash_size * 2);

    list_init_add(&weave->hash[block->addr % weave->hash_size], &block->chain);
    list_init_add(blocks_list(weave, PAGE(block->addr), 0), &block->page[0]);
    if (PAGE(block->addr) != PAGE(block->end_addr))
        list_init_add(blocks_list(weave, PAGE(block->end_addr), 1), &block->page[1]);
}

static struct threaded_block *threaded_lookup(struct weave *weave, addr_t addr) {
    struct list *bucket = &weave->hash[addr % weave->hash_size];
    if (list_null(bucket))
        return NULL;
    struct threaded_block *block;
    list_for_each_entry(bucket, block, chain) {
        if (block->addr == addr)
            return block;
    }
    return NULL;
}

static struct threaded_block *threaded_block_compile(addr_t ip, struct tlb *tlb) {
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
static void threaded_block_disconnect(struct weave *weave, struct threaded_block *block) {
    if (weave != NULL) {
        weave->mem_used -= block->used;
        weave->num_blocks--;
    }
    list_remove(&block->chain);
    for (int i = 0; i <= 1; i++) {
        list_remove(&block->page[i]);
        list_remove_safe(&block->jumps_from_links[i]);

        struct threaded_block *prev_block, *tmp;
        list_for_each_entry_safe(&block->jumps_from[i], prev_block, tmp, jumps_from_links[i]) {
            if (prev_block->jump_ip[i] != NULL)
                *prev_block->jump_ip[i] = prev_block->old_jump_ip[i];
            list_remove(&prev_block->jumps_from_links[i]);
        }
    }
}

static void threaded_block_free(struct weave *weave, struct threaded_block *block) {
    threaded_block_disconnect(weave, block);
    free(block);
}

static void threaded_free_jetsam(struct weave *weave) {
    struct threaded_block *block, *tmp;
    list_for_each_entry_safe(&weave->jetsam, block, tmp, jetsam) {
        list_remove(&block->jetsam);
        free(block);
    }
}

int threaded_enter(struct threaded_block *block, struct threaded_frame *frame, struct tlb *tlb);

static inline size_t threaded_cache_hash(addr_t ip) {
    return (ip ^ (ip >> 12)) % THREADED_CACHE_SIZE;
}

static int cpu_step_to_interrupt(struct cpu_state *cpu, struct tlb *tlb) {
    struct weave *weave = cpu->mmu->weave;
    read_wrlock(&weave->jetsam_lock);

    struct threaded_block **cache = calloc(THREADED_CACHE_SIZE, sizeof(*cache));
    struct threaded_frame *frame = malloc(sizeof(struct threaded_frame));
    memset(frame, 0, sizeof(*frame));
    frame->cpu = *cpu;
    assert(weave->mmu == cpu->mmu);

    int interrupt = INT_NONE;
    while (interrupt == INT_NONE) {
        addr_t ip = frame->cpu.eip;
        size_t cache_index = threaded_cache_hash(ip);
        struct threaded_block *block = cache[cache_index];
        if (block == NULL || block->addr != ip) {
            lock(&weave->lock);
            block = threaded_lookup(weave, ip);
            if (block == NULL) {
                block = threaded_block_compile(ip, tlb);
                threaded_insert(weave, block);
            } else {
                TRACE("%d %08x --- missed cache\n", current_pid(), ip);
            }
            cache[cache_index] = block;
            unlock(&weave->lock);
        }
        struct threaded_block *last_block = frame->last_block;
        if (last_block != NULL &&
                (last_block->jump_ip[0] != NULL ||
                 last_block->jump_ip[1] != NULL)) {
            lock(&weave->lock);
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

            unlock(&weave->lock);
        }
        frame->last_block = block;

        // block may be jetsam, but that's ok, because it can't be freed until
        // every thread on this weave is not executing anything

        TRACE("%d %08x --- cycle %ld\n", current_pid(), ip, frame->cpu.cycle);

        interrupt = threaded_enter(block, frame, tlb);
        if (interrupt == INT_NONE && __atomic_exchange_n(cpu->poked_ptr, false, __ATOMIC_SEQ_CST))
            interrupt = INT_TIMER;
        if (interrupt == INT_NONE && ++frame->cpu.cycle % (1 << 10) == 0)
            interrupt = INT_TIMER;
        *cpu = frame->cpu;
    }

    free(frame);
    free(cache);
    read_wrunlock(&weave->jetsam_lock);
    return interrupt;
}

static int cpu_single_step(struct cpu_state *cpu, struct tlb *tlb) {
    struct gen_state state;
    gen_start(cpu->eip, &state);
    gen_step(&state, tlb);
    gen_exit(&state);
    gen_end(&state);

    struct threaded_block *block = state.block;
    struct threaded_frame frame = {.cpu = *cpu};
    int interrupt = threaded_enter(block, &frame, tlb);
    *cpu = frame.cpu;
    threaded_block_free(NULL, block);
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

    struct weave *weave = cpu->mmu->weave;
    lock(&weave->lock);
    if (!list_empty(&weave->jetsam)) {
        // write-lock the jetsam_lock to wait until other weave threads get to
        // this point, so they will all clear out their block pointers
        // TODO: use RCU for better performance
        unlock(&weave->lock);
        write_wrlock(&weave->jetsam_lock);
        lock(&weave->lock);
        threaded_free_jetsam(weave);
        write_wrunlock(&weave->jetsam_lock);
    }
    unlock(&weave->lock);

    return interrupt;
}

void cpu_poke(struct cpu_state *cpu) {
    __atomic_store_n(cpu->poked_ptr, true, __ATOMIC_SEQ_CST);
}
