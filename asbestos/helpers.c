#include <time.h>
#include "emu/cpu.h"
#include "emu/cpuid.h"
#include "emu/tlb.h"
#include "util/sync.h"

void helper_cpuid(dword_t *a, dword_t *b, dword_t *c, dword_t *d) {
    do_cpuid(a, b, c, d);
}

void helper_rdtsc(struct cpu_state *cpu) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t tsc = now.tv_sec * 1000000000l + now.tv_nsec;
    cpu->eax = tsc & 0xffffffff;
    cpu->edx = tsc >> 32;
}

void helper_expand_flags(struct cpu_state *cpu) {
    expand_flags(cpu);
}

void helper_collapse_flags(struct cpu_state *cpu) {
    collapse_flags(cpu);
}

// lock cmpxchg8b on an address that isn't 8-byte aligned, which x86 allows and
// the i386 ABI produces (it aligns 64-bit struct fields to 4 bytes). The
// gadget's exclusive load needs 8-byte alignment, so this does the exchange
// under a lock instead. Returns nonzero on a fault, with tlb->segfault_addr set.
static lock_t cmpxchg8b_lock = LOCK_INITIALIZER;
int helper_cmpxchg8b_unaligned(struct cpu_state *cpu, struct tlb *tlb, addr_t addr) {
    uint64_t expected = ((uint64_t) cpu->edx << 32) | cpu->eax;
    uint64_t desired = ((uint64_t) cpu->ecx << 32) | cpu->ebx;
    uint64_t value;
    bool ok;
    lock(&cmpxchg8b_lock);
    ok = tlb_read(tlb, addr, &value, sizeof(value));
    if (ok && value == expected)
        ok = tlb_write(tlb, addr, &desired, sizeof(desired));
    unlock(&cmpxchg8b_lock);
    if (!ok)
        return 1;
    cpu->eax = (dword_t) value;
    cpu->edx = (dword_t) (value >> 32);
    cpu->zf = value == expected;
    cpu->zf_res = 0;
    return 0;
}
