// 64-bit guest: an x86_64 interpreter in portable C.
//
// Each instruction is decoded into a struct insn and then executed by exec().
// Nothing here generates code, so it can run on iOS. Guest memory is reached
// through ish's TLB exactly as asbestos does.
//
// Rules that keep instructions restartable after a page fault (the kernel
// maps the page and runs the instruction again):
//   - a memory access that faults leaves through fault() (longjmp), so
//   - all memory accesses of an instruction happen before it changes any
//     register or flag it also reads, and rip moves last.
#include <setjmp.h>
#include <math.h>
#include <fenv.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "emu/cpu.h"
#include "emu/tlb.h"
#include "emu/interrupt.h"
#include "emu/fpu.h"
#include "emu/float80.h"
#include "asbestos/asbestos.h"

#undef likely
#undef unlikely
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

enum {
    P_66 = 1, P_F3 = 2, P_F2 = 4, P_LOCK = 8,
    P_ADDR32 = 16, P_FS = 32, P_GS = 64, P_REX = 128,
};
// register slots behind regs[16] (see cpu.h): usable as base or index
#define RFS 17
#define RGS 18
#define RNONE 19 // reads as 0

struct ictx;
struct insn;
typedef int (*insn_fn)(struct ictx *c, const struct insn *in, struct cpu_state *cpu);

struct insn {
    insn_fn fn;
    int64_t disp; // absolute for rip-relative operands
    int64_t imm;
    uint16_t op; // 0x000-0x0ff one-byte map, 0x100-0x1ff 0F map, above: unsupported
    uint8_t len;
    uint8_t opsz; // 2, 4 or 8: size of a "v" operand
    uint8_t pfx;
    uint8_t mod, reg, rm; // reg and rm include the REX bits; mod == 3 means register
    uint8_t base, index, scale;
};

#define WLOG_MAX 8
struct wlog {
    uint64_t addr;
    unsigned size;
    uint8_t old[16], new[16];
};

struct ictx {
    struct cpu_state *cpu;
    struct tlb *tlb;
    struct space *space;
    // Flags are kept as the values they are derived from:
    uint64_t zr; // ZF = (zr == 0)
    int64_t sn;  // SF = (sn < 0)
    uint8_t pr;  // PF = even parity of pr
    uint8_t aux; // AF = bit 4
    uint8_t cf, of, df;
    uint32_t flags_other;
    jmp_buf fault;
    // lockstep checking against the reference CPU
    bool single; // execute one instruction (one iteration of a rep)
    bool log;
    bool log_overflow;
    int nlog;
    uint32_t undef; // flags the last instruction left undefined
    struct wlog wl[WLOG_MAX];
    struct insn last;
};

// ---------------------------------------------------------------- flags

#define ARITH_FLAGS 0x8d5u
#define F_CF 1u
#define F_PF 4u
#define F_AF 0x10u
#define F_ZF 0x40u
#define F_SF 0x80u
#define F_DF 0x400u
#define F_OF 0x800u

static forceinline void flags_load(struct ictx *c, uint32_t ef) {
    c->cf = ef & 1;
    c->pr = (ef & F_PF) ? 0 : 1;
    c->aux = ef & F_AF;
    c->zr = !(ef & F_ZF);
    c->sn = (ef & F_SF) ? -1 : 0;
    c->df = (ef >> 10) & 1;
    c->of = (ef >> 11) & 1;
    c->flags_other = ef & ~(ARITH_FLAGS | F_DF);
}
static forceinline uint32_t flags_save(struct ictx *c) {
    return (c->flags_other & ~(ARITH_FLAGS | F_DF)) | 2 | c->cf
        | (__builtin_parity(c->pr) ? 0 : F_PF)
        | (c->aux & F_AF)
        | (c->zr == 0 ? F_ZF : 0)
        | (c->sn < 0 ? F_SF : 0)
        | ((uint32_t) c->df << 10) | ((uint32_t) c->of << 11);
}

static forceinline bool cond(struct ictx *c, int cc) {
    bool r;
    switch ((cc >> 1) & 7) {
        case 0: r = c->of; break;
        case 1: r = c->cf; break;
        case 2: r = c->zr == 0; break;
        case 3: r = c->cf || c->zr == 0; break;
        case 4: r = c->sn < 0; break;
        case 5: r = !__builtin_parity(c->pr); break;
        case 6: r = (c->sn < 0) != c->of; break;
        default: r = c->zr == 0 || (c->sn < 0) != c->of; break;
    }
    return r ^ (cc & 1);
}

static forceinline uint64_t szmask(int sz) {
    return sz == 8 ? ~0ULL : (1ULL << (sz * 8)) - 1;
}
static forceinline int64_t sx(uint64_t v, int sz) {
    switch (sz) {
        case 1: return (int8_t) v;
        case 2: return (int16_t) v;
        case 4: return (int32_t) v;
        default: return (int64_t) v;
    }
}
static forceinline void setszp(struct ictx *c, uint64_t r, int sz) {
    c->zr = r & szmask(sz);
    c->sn = sx(r, sz);
    c->pr = (uint8_t) r;
}

// ---------------------------------------------------------------- memory

#define GEN_BITS 16
#define CODE_LEAF_PAGES (1u << 15)
#define GEN_INDEX(page) (((page) ^ ((page) >> GEN_BITS)) & ((1u << GEN_BITS) - 1))
struct space {
    uint64_t id;
    uint32_t gens[1u << GEN_BITS];
    // which pages instructions were decoded from: one bit per page, in
    // bitmaps of CODE_LEAF_PAGES pages that exist only where there is code
    uint8_t *code[MEM_PAGES / CODE_LEAF_PAGES];
};
static forceinline bool space_has_code(const struct space *sp, addr_t addr) {
    page_t page = PAGE(addr);
    if (page >= MEM_PAGES)
        return false;
    const uint8_t *leaf = __atomic_load_n(&sp->code[page / CODE_LEAF_PAGES], __ATOMIC_ACQUIRE);
    return leaf != NULL && (leaf[(page % CODE_LEAF_PAGES) >> 3] & (1u << (page & 7)));
}


static __attribute__((__noreturn__, noinline)) void fault(struct ictx *c, uint64_t addr, bool write) {
    struct cpu_state *cpu = c->cpu;
    cpu->segfault_addr64 = 0;
    cpu->segfault_addr = addr;
    cpu->segfault_was_write = write;
    _longjmp(c->fault, 1);
}

// host pointer for one guest address, valid up to the end of its page
static forceinline void *xlat_r(struct ictx *c, uint64_t addr) {
    void *p = __tlb_read_ptr(c->tlb, (addr_t) addr);
    if (unlikely(p == NULL))
        fault(c, addr, false);
    return p;
}
// A write to a page instructions were decoded from must be seen every time
// (self-modifying code, JIT compilers): the miss path tells memory.c, which
// drops the decoded blocks, and such a page never gets a cached writable TLB
// entry, so the next write to it takes the miss path again.
static __attribute__((noinline)) void *xlat_w_miss(struct ictx *c, uint64_t addr) {
    void *p = tlb_handle_miss(c->tlb, (addr_t) addr, MEM_WRITE);
    if (p == NULL)
        fault(c, addr, true);
    if (unlikely(space_has_code(c->space, addr))) {
        // (the TLB's own miss path does not report writes; only faults do)
        __atomic_fetch_add(&c->space->gens[GEN_INDEX(PAGE((addr_t) addr))], 1, __ATOMIC_RELAXED);
        c->tlb->entries[TLB_INDEX(addr)].page_if_writable = TLB_PAGE_EMPTY;
    }
    return p;
}
static forceinline void *xlat_w(struct ictx *c, uint64_t addr) {
    struct tlb_entry entry = c->tlb->entries[TLB_INDEX(addr)];
    if (likely(entry.page_if_writable == TLB_PAGE(addr))) {
        c->tlb->dirty_page = TLB_PAGE(addr);
        return (void *) (entry.data_minus_addr + addr);
    }
    return xlat_w_miss(c, addr);
}

static __attribute__((noinline)) void mread_n(struct ictx *c, uint64_t addr, void *out, unsigned n) {
    uint8_t *o = out;
    while (n) {
        unsigned chunk = PAGE_SIZE - (addr & (PAGE_SIZE - 1));
        if (chunk > n)
            chunk = n;
        memcpy(o, xlat_r(c, addr), chunk);
        addr += chunk; o += chunk; n -= chunk;
    }
}

static __attribute__((noinline)) void wlog_add(struct ictx *c, uint64_t addr, unsigned n) {
    while (n) {
        unsigned chunk = n > 16 ? 16 : n;
        if (c->nlog == WLOG_MAX) {
            c->log_overflow = true;
            return;
        }
        struct wlog *w = &c->wl[c->nlog++];
        w->addr = addr;
        w->size = chunk;
        mread_n(c, addr, w->old, chunk);
        addr += chunk; n -= chunk;
    }
}

// Make sure [addr, addr+n) is writable before anything is written.
static forceinline void wprobe(struct ictx *c, uint64_t addr, unsigned n) {
    uint64_t end = addr + n - 1;
    xlat_w(c, addr);
    for (uint64_t p = (addr | (PAGE_SIZE - 1)) + 1; p <= end; p += PAGE_SIZE)
        xlat_w(c, p);
}

static __attribute__((noinline)) void mwrite_n(struct ictx *c, uint64_t addr, const void *in, unsigned n) {
    const uint8_t *i = in;
    wprobe(c, addr, n);
    if (unlikely(c->log))
        wlog_add(c, addr, n);
    while (n) {
        unsigned chunk = PAGE_SIZE - (addr & (PAGE_SIZE - 1));
        if (chunk > n)
            chunk = n;
        memcpy(xlat_w(c, addr), i, chunk);
        addr += chunk; i += chunk; n -= chunk;
    }
}

static forceinline uint64_t ld(const void *p, int sz) {
    switch (sz) {
        case 1: return *(const uint8_t *) p;
        case 2: { uint16_t v; memcpy(&v, p, 2); return v; }
        case 4: { uint32_t v; memcpy(&v, p, 4); return v; }
        default: { uint64_t v; memcpy(&v, p, 8); return v; }
    }
}
static forceinline void st(void *p, int sz, uint64_t v) {
    switch (sz) {
        case 1: *(uint8_t *) p = (uint8_t) v; break;
        case 2: { uint16_t x = (uint16_t) v; memcpy(p, &x, 2); break; }
        case 4: { uint32_t x = (uint32_t) v; memcpy(p, &x, 4); break; }
        default: memcpy(p, &v, 8); break;
    }
}

static forceinline uint64_t mrd(struct ictx *c, uint64_t addr, int sz) {
    if (unlikely((addr & (PAGE_SIZE - 1)) + sz > PAGE_SIZE)) {
        uint64_t v = 0;
        mread_n(c, addr, &v, sz);
        return v;
    }
    return ld(xlat_r(c, addr), sz);
}
static forceinline void mwr(struct ictx *c, uint64_t addr, int sz, uint64_t v) {
    if (unlikely((addr & (PAGE_SIZE - 1)) + sz > PAGE_SIZE)) {
        mwrite_n(c, addr, &v, sz);
        return;
    }
    void *p = xlat_w(c, addr);
    if (unlikely(c->log))
        wlog_add(c, addr, sz);
    st(p, sz, v);
}

// Pointer for a read-modify-write of sz bytes. NULL means the operand crosses
// a page; both pages are writable by then, use mrd/mwr.
static forceinline void *wptr(struct ictx *c, uint64_t addr, int sz) {
    if (unlikely((addr & (PAGE_SIZE - 1)) + sz > PAGE_SIZE)) {
        wprobe(c, addr, sz);
        return NULL;
    }
    void *p = xlat_w(c, addr);
    if (unlikely(c->log))
        wlog_add(c, addr, sz);
    return p;
}

// atomic access for locked instructions
static forceinline uint64_t aload(void *p, int sz) {
    switch (sz) {
        case 1: return __atomic_load_n((uint8_t *) p, __ATOMIC_SEQ_CST);
        case 2: return __atomic_load_n((uint16_t *) p, __ATOMIC_SEQ_CST);
        case 4: return __atomic_load_n((uint32_t *) p, __ATOMIC_SEQ_CST);
        default: return __atomic_load_n((uint64_t *) p, __ATOMIC_SEQ_CST);
    }
}
static forceinline bool acas(void *p, int sz, uint64_t old, uint64_t new) {
#if !defined(__x86_64__)
    if (unlikely((uintptr_t) p & (sz - 1))) {
        // misaligned: not atomic (arm64 cannot do it)
        st(p, sz, new);
        return true;
    }
#endif
    switch (sz) {
        case 1: { uint8_t o = (uint8_t) old; return __atomic_compare_exchange_n((uint8_t *) p, &o, (uint8_t) new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
        case 2: { uint16_t o = (uint16_t) old; return __atomic_compare_exchange_n((uint16_t *) p, &o, (uint16_t) new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
        case 4: { uint32_t o = (uint32_t) old; return __atomic_compare_exchange_n((uint32_t *) p, &o, (uint32_t) new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
        default: { uint64_t o = old; return __atomic_compare_exchange_n((uint64_t *) p, &o, new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
    }
}

// ---------------------------------------------------------------- registers

static forceinline uint8_t *r8p(struct cpu_state *cpu, const struct insn *in, int r) {
    if (!(in->pfx & P_REX) && r >= 4 && r < 8)
        return (uint8_t *) &cpu->regs[r - 4] + 1; // ah ch dh bh
    return (uint8_t *) &cpu->regs[r];
}
static forceinline uint64_t rget(struct cpu_state *cpu, const struct insn *in, int r, int sz) {
    switch (sz) {
        case 1: return *r8p(cpu, in, r);
        case 2: return (uint16_t) cpu->regs[r];
        case 4: return (uint32_t) cpu->regs[r];
        default: return cpu->regs[r];
    }
}
static forceinline void rset(struct cpu_state *cpu, const struct insn *in, int r, int sz, uint64_t v) {
    switch (sz) {
        case 1: *r8p(cpu, in, r) = (uint8_t) v; break;
        case 2: *(uint16_t *) &cpu->regs[r] = (uint16_t) v; break;
        case 4: cpu->regs[r] = (uint32_t) v; break;
        default: cpu->regs[r] = v; break;
    }
}

static forceinline uint64_t ea_noseg(struct ictx *c, const struct insn *in) {
    const uint64_t *R = (const uint64_t *) ((const char *) c->cpu + offsetof(struct cpu_state, regs));
    uint64_t a = (uint64_t) in->disp + R[in->base] + (R[in->index] << in->scale);
    if (unlikely(in->pfx & P_ADDR32))
        a = (uint32_t) a;
    return a;
}
static forceinline uint64_t segbase(struct ictx *c, const struct insn *in) {
    if (in->pfx & P_FS)
        return c->cpu->fs_base;
    if (in->pfx & P_GS)
        return c->cpu->gs_base;
    return 0;
}
static forceinline uint64_t ea(struct ictx *c, const struct insn *in) {
    uint64_t a = ea_noseg(c, in);
    if (unlikely(in->pfx & (P_FS | P_GS)))
        a += segbase(c, in);
    return a;
}

static forceinline uint64_t eget(struct ictx *c, const struct insn *in, int sz) {
    if (in->mod == 3)
        return rget(c->cpu, in, in->rm, sz);
    return mrd(c, ea(c, in), sz);
}
static forceinline void eset(struct ictx *c, const struct insn *in, int sz, uint64_t v) {
    if (in->mod == 3)
        rset(c->cpu, in, in->rm, sz, v);
    else
        mwr(c, ea(c, in), sz, v);
}

static forceinline void push64(struct ictx *c, uint64_t v) {
    mwr(c, c->cpu->rsp - 8, 8, v);
    c->cpu->rsp -= 8;
}

// ---------------------------------------------------------------- arithmetic

enum { ALU_ADD, ALU_OR, ALU_ADC, ALU_SBB, ALU_AND, ALU_SUB, ALU_XOR, ALU_CMP };

static forceinline uint64_t alu(struct ictx *c, int op, int sz, uint64_t a, uint64_t b, unsigned cin) {
    uint64_t m = szmask(sz), r;
    int msb = sz * 8 - 1;
    a &= m; b &= m;
    switch (op) {
        case ALU_ADD:
            r = (a + b) & m;
            c->cf = r < a;
            c->of = (((a ^ r) & (b ^ r)) >> msb) & 1;
            c->aux = (uint8_t) (a ^ b ^ r);
            break;
        case ALU_ADC:
            r = (a + b + cin) & m;
            c->cf = cin ? r <= a : r < a;
            c->of = (((a ^ r) & (b ^ r)) >> msb) & 1;
            c->aux = (uint8_t) (a ^ b ^ r);
            break;
        case ALU_SUB: case ALU_CMP:
            r = (a - b) & m;
            c->cf = a < b;
            c->of = (((a ^ b) & (a ^ r)) >> msb) & 1;
            c->aux = (uint8_t) (a ^ b ^ r);
            break;
        case ALU_SBB:
            r = (a - b - cin) & m;
            c->cf = cin ? a <= b : a < b;
            c->of = (((a ^ b) & (a ^ r)) >> msb) & 1;
            c->aux = (uint8_t) (a ^ b ^ r);
            break;
        case ALU_OR: r = a | b; goto logic;
        case ALU_AND: r = a & b; goto logic;
        default: r = a ^ b;
        logic:
            c->cf = c->of = 0;
            c->aux = 0;
            c->undef = F_AF;
            break;
    }
    setszp(c, r, sz);
    return r;
}

static forceinline uint64_t incdec(struct ictx *c, bool dec, int sz, uint64_t a) {
    uint64_t m = szmask(sz), r;
    uint64_t top = 1ULL << (sz * 8 - 1);
    a &= m;
    if (dec) {
        r = (a - 1) & m;
        c->of = r == top - 1;
    } else {
        r = (a + 1) & m;
        c->of = r == top;
    }
    c->aux = (uint8_t) (a ^ 1 ^ r);
    setszp(c, r, sz);
    return r;
}

static forceinline uint64_t neg_op(struct ictx *c, int sz, uint64_t a) {
    uint64_t m = szmask(sz);
    a &= m;
    uint64_t r = (0 - a) & m;
    c->cf = a != 0;
    c->of = r == (1ULL << (sz * 8 - 1));
    c->aux = (uint8_t) (a ^ r);
    setszp(c, r, sz);
    return r;
}

static uint64_t shift_op(struct ictx *c, int kind, int sz, uint64_t a, unsigned cnt) {
    int bits = sz * 8;
    uint64_t m = szmask(sz), r;
    a &= m;
    cnt &= sz == 8 ? 63 : 31;
    if (cnt == 0)
        return a;
    unsigned n;
    switch (kind) {
        case 0: // rol
            n = cnt % bits;
            r = n ? ((a << n) | (a >> (bits - n))) & m : a;
            c->cf = r & 1;
            c->of = ((r >> (bits - 1)) ^ r) & 1;
            if (cnt != 1) c->undef = F_OF;
            return r;
        case 1: // ror
            n = cnt % bits;
            r = n ? ((a >> n) | (a << (bits - n))) & m : a;
            c->cf = (r >> (bits - 1)) & 1;
            c->of = ((r >> (bits - 1)) ^ (r >> (bits - 2))) & 1;
            if (cnt != 1) c->undef = F_OF;
            return r;
        case 2: // rcl
            n = cnt % (bits + 1);
            r = a;
            for (unsigned k = 0; k < n; k++) {
                unsigned newcf = (r >> (bits - 1)) & 1;
                r = ((r << 1) | c->cf) & m;
                c->cf = newcf;
            }
            c->of = ((r >> (bits - 1)) ^ c->cf) & 1;
            if (cnt != 1) c->undef = F_OF;
            return r;
        case 3: // rcr
            n = cnt % (bits + 1);
            r = a;
            for (unsigned k = 0; k < n; k++) {
                unsigned newcf = r & 1;
                r = (r >> 1) | ((uint64_t) c->cf << (bits - 1));
                c->cf = newcf;
            }
            c->of = ((r >> (bits - 1)) ^ (r >> (bits - 2))) & 1;
            if (cnt != 1) c->undef = F_OF;
            return r;
        case 4: case 6: // shl
            if (cnt > (unsigned) bits) {
                r = 0;
                c->cf = 0;
            } else {
                c->cf = (a >> (bits - cnt)) & 1;
                r = cnt == (unsigned) bits ? 0 : (a << cnt) & m;
            }
            c->of = ((r >> (bits - 1)) ^ c->cf) & 1;
            break;
        case 5: // shr
            if (cnt > (unsigned) bits) {
                r = 0;
                c->cf = 0;
            } else {
                c->cf = (a >> (cnt - 1)) & 1;
                r = cnt == (unsigned) bits ? 0 : a >> cnt;
            }
            c->of = (((cnt - 1 >= 64 ? 0 : a >> (cnt - 1)) ^ r) >> (bits - 1)) & 1;
            break;
        default: { // sar
            int64_t s = sx(a, sz);
            c->cf = (s >> (cnt - 1 > 63 ? 63 : cnt - 1)) & 1;
            r = (uint64_t) (s >> (cnt > 63 ? 63 : cnt)) & m;
            c->of = 0;
            break;
        }
    }
    c->aux = 0;
    c->undef = F_AF | (cnt != 1 ? F_OF : 0);
    setszp(c, r, sz);
    return r;
}

static uint64_t shiftd_op(struct ictx *c, bool right, int sz, uint64_t a, uint64_t b, unsigned cnt) {
    int bits = sz * 8;
    uint64_t m = szmask(sz), r;
    a &= m; b &= m;
    cnt &= sz == 8 ? 63 : 31;
    if (cnt == 0 || cnt > (unsigned) bits)
        return a;
    if (right) {
        r = ((a >> cnt) | (cnt == (unsigned) bits ? b : b << (bits - cnt))) & m;
        if (cnt == (unsigned) bits) r = b;
        c->cf = (a >> (cnt - 1)) & 1;
    } else {
        r = ((cnt == (unsigned) bits ? 0 : a << cnt) | (b >> (bits - cnt))) & m;
        c->cf = (a >> (bits - cnt)) & 1;
    }
    c->of = ((a ^ r) >> (bits - 1)) & 1;
    c->aux = 0;
    c->undef = F_AF | (cnt != 1 ? F_OF : 0);
    setszp(c, r, sz);
    return r;
}

static forceinline uint64_t imul_op(struct ictx *c, int sz, uint64_t a, uint64_t b) {
    uint64_t r;
    if (sz == 8) {
        __int128 full = (__int128) (int64_t) a * (int64_t) b;
        r = (uint64_t) full;
        c->cf = c->of = (__int128) (int64_t) r != full;
    } else {
        int64_t full = sx(a, sz) * sx(b, sz);
        r = (uint64_t) full & szmask(sz);
        c->cf = c->of = sx(r, sz) != full;
    }
    setszp(c, r, sz);
    c->aux = 0;
    c->undef = F_SF | F_ZF | F_AF | F_PF;
    return r;
}

// Read-modify-write of an r/m operand. EXPR computes `r` from `a` and may be
// evaluated more than once under a lock prefix (compare-and-swap loop).
#define RMW_E(c, in, sz, EXPR) do { \
    if ((in)->mod == 3) { \
        a = rget(cpu, in, (in)->rm, sz); \
        EXPR; \
        rset(cpu, in, (in)->rm, sz, r); \
    } else { \
        uint64_t addr_ = ea(c, in); \
        void *p_ = wptr(c, addr_, sz); \
        if (unlikely(p_ == NULL)) { \
            a = mrd(c, addr_, sz); \
            EXPR; \
            mwr(c, addr_, sz, r); \
        } else if ((in)->pfx & P_LOCK) { \
            do { \
                a = aload(p_, sz); \
                EXPR; \
            } while (!acas(p_, sz, a, r)); \
        } else { \
            a = ld(p_, sz); \
            EXPR; \
            st(p_, sz, r); \
        } \
    } \
} while (0)

static forceinline void alu_E(struct ictx *c, const struct insn *in, int op, int sz, uint64_t b) {
    struct cpu_state *cpu = c->cpu;
    unsigned cin = c->cf;
    uint64_t a, r;
    if (op == ALU_CMP) {
        a = eget(c, in, sz);
        alu(c, op, sz, a, b, cin);
        return;
    }
    RMW_E(c, in, sz, r = alu(c, op, sz, a, b, cin));
}

static void do_cpuid(struct cpu_state *cpu) {
    uint32_t leaf = (uint32_t) cpu->rax;
    uint32_t a = 0, b = 0, cx = 0, d = 0;
    if (leaf >= 0x80000000) {
        if (leaf == 0x80000000) {
            a = 0x80000001;
        } else if (leaf == 0x80000001) {
            cx = 1 << 0; // lahf/sahf in long mode
            d = (1 << 11) | (1 << 20) | (1 << 29); // syscall, nx, long mode
        }
    } else if (leaf == 0) {
        a = 1;
        b = 0x756e6547; d = 0x49656e69; cx = 0x6c65746e; // GenuineIntel
    } else {
        // leaf 1 (and anything above the highest leaf)
        a = 0x000006f0;
        b = 8 << 8; // clflush line size
        // x86-64-v2: sse3 ssse3 cx16 sse4.1 sse4.2 popcnt, plus movbe
        cx = (1 << 0) | (1 << 9) | (1 << 13) | (1 << 19) | (1 << 20) | (1 << 22) | (1 << 23);
        d = (1 << 0) | (1 << 4) | (1 << 8) | (1 << 15) | (1 << 19) // fpu tsc cx8 cmov clflush
            | (1 << 23) | (1 << 24) | (1 << 25) | (1 << 26); // mmx fxsr sse sse2
    }
    cpu->rax = a; cpu->rbx = b; cpu->rcx = cx; cpu->rdx = d;
}

static int undefined_insn(struct ictx *c, const struct insn *in) {
    struct cpu_state *cpu = c->cpu;
    uint8_t bytes[16] = {0};
    for (int i = 0; i < 15; i++) {
        void *p = __tlb_read_ptr(c->tlb, cpu->rip + i);
        if (p == NULL)
            break;
        bytes[i] = *(uint8_t *) p;
    }
    fprintf(stderr, "interp64: unimplemented instruction at %#llx (op %#x pfx %#x reg %d):",
            (unsigned long long) cpu->rip, in->op, in->pfx, in->reg);
    for (int i = 0; i < 15; i++)
        fprintf(stderr, " %02x", bytes[i]);
    fprintf(stderr, "\n");
    return INT_UNDEFINED;
}

#include "emu/interp64_sse.h"
#include "emu/interp64_x87.h"
#include "emu/interp64_sse4.h"

// ---------------------------------------------------------------- string instructions

static int do_string(struct ictx *c, const struct insn *in, uint64_t next) {
    struct cpu_state *cpu = c->cpu;
    int kind = in->op & 0xfe;
    int sz = (in->op & 1) ? in->opsz : 1;
    bool rep = in->pfx & (P_F3 | P_F2);
    bool a32 = in->pfx & P_ADDR32;
    int64_t d = c->df ? -sz : sz;
    uint64_t seg = segbase(c, in);
    uint64_t count = rep ? (a32 ? (uint32_t) cpu->rcx : cpu->rcx) : 1;

    if (rep && !a32 && !c->df && !c->single) {
        if (kind == 0xa4) {
            while (count) {
                uint64_t si = cpu->rsi + seg, di = cpu->rdi;
                uint64_t n = count, lim;
                lim = (PAGE_SIZE - (si & (PAGE_SIZE - 1))) / sz; if (n > lim) n = lim;
                lim = (PAGE_SIZE - (di & (PAGE_SIZE - 1))) / sz; if (n > lim) n = lim;
                if (n == 0) {
                    mwr(c, di, sz, mrd(c, si, sz));
                    n = 1;
                } else {
                    // destination first: making it writable may replace a
                    // copy-on-write page the source pointer would point into
                    uint8_t *dp = xlat_w(c, di);
                    const uint8_t *sp = xlat_r(c, si);
                    size_t bytes = n * sz;
                    if (di - si >= bytes)
                        memmove(dp, sp, bytes);
                    else
                        for (size_t k = 0; k < bytes; k += sz)
                            st(dp + k, sz, ld(sp + k, sz));
                }
                cpu->rsi += n * sz; cpu->rdi += n * sz;
                count -= n;
                cpu->rcx = count;
            }
            cpu->rip = next;
            return INT_NONE;
        }
        if (kind == 0xaa) {
            uint64_t v = cpu->rax;
            while (count) {
                uint64_t di = cpu->rdi;
                uint64_t n = count, lim;
                lim = (PAGE_SIZE - (di & (PAGE_SIZE - 1))) / sz; if (n > lim) n = lim;
                if (n == 0) {
                    mwr(c, di, sz, v);
                    n = 1;
                } else {
                    uint8_t *dp = xlat_w(c, di);
                    if (sz == 1)
                        memset(dp, (uint8_t) v, n);
                    else
                        for (size_t k = 0; k < n * sz; k += sz)
                            st(dp + k, sz, v);
                }
                cpu->rdi += n * sz;
                count -= n;
                cpu->rcx = count;
            }
            cpu->rip = next;
            return INT_NONE;
        }
    }

#define SI_ (a32 ? (uint32_t) cpu->rsi : cpu->rsi)
#define DI_ (a32 ? (uint32_t) cpu->rdi : cpu->rdi)
#define ADV(reg) do { if (a32) cpu->reg = (uint32_t) (cpu->reg + d); else cpu->reg += d; } while (0)
    while (count) {
        uint64_t a, b;
        switch (kind) {
            case 0xa4:
                a = mrd(c, SI_ + seg, sz);
                mwr(c, DI_, sz, a);
                ADV(rsi); ADV(rdi);
                break;
            case 0xa6:
                a = mrd(c, SI_ + seg, sz);
                b = mrd(c, DI_, sz);
                alu(c, ALU_CMP, sz, a, b, 0);
                ADV(rsi); ADV(rdi);
                break;
            case 0xaa:
                mwr(c, DI_, sz, cpu->rax);
                ADV(rdi);
                break;
            case 0xac:
                a = mrd(c, SI_ + seg, sz);
                rset(cpu, in, 0, sz, a);
                ADV(rsi);
                break;
            default: // scas
                a = rget(cpu, in, 0, sz);
                b = mrd(c, DI_, sz);
                alu(c, ALU_CMP, sz, a, b, 0);
                ADV(rdi);
                break;
        }
        count--;
        if (!rep)
            break;
        if (a32) cpu->rcx = (uint32_t) count; else cpu->rcx = count;
        if (kind == 0xa6 || kind == 0xae) {
            bool zf = c->zr == 0;
            if ((in->pfx & P_F3) ? !zf : zf)
                break;
        }
        if (c->single)
            return INT_NONE; // one iteration per step, like the reference CPU
    }
#undef SI_
#undef DI_
#undef ADV
    cpu->rip = next;
    return INT_NONE;
}

static void bitop(struct ictx *c, const struct insn *in, int sz, int kind, uint64_t off, bool off_is_reg) {
    struct cpu_state *cpu = c->cpu;
    int bits = sz * 8;
    unsigned bit = off & (bits - 1);
    uint64_t a, r, mask = 1ULL << bit;
    unsigned cfv = 0;
#define BITEXPR do { cfv = (a >> bit) & 1; r = kind == 1 ? a | mask : kind == 2 ? a & ~mask : a ^ mask; } while (0)
    if (in->mod == 3) {
        a = rget(cpu, in, in->rm, sz);
        if (kind == 0) {
            cfv = (a >> bit) & 1;
        } else {
            BITEXPR;
            rset(cpu, in, in->rm, sz, r);
        }
    } else {
        uint64_t addr = ea(c, in);
        if (off_is_reg)
            addr += (uint64_t) ((sx(off, sz) >> (sz == 2 ? 4 : sz == 4 ? 5 : 6)) * sz);
        if (kind == 0) {
            a = mrd(c, addr, sz);
            cfv = (a >> bit) & 1;
        } else {
            void *p = wptr(c, addr, sz);
            if (p == NULL) {
                a = mrd(c, addr, sz);
                BITEXPR;
                mwr(c, addr, sz, r);
            } else if (in->pfx & P_LOCK) {
                do {
                    a = aload(p, sz);
                    BITEXPR;
                } while (!acas(p, sz, a, r));
            } else {
                a = ld(p, sz);
                BITEXPR;
                st(p, sz, r);
            }
        }
    }
#undef BITEXPR
    c->cf = cfv;
    c->undef = F_OF | F_SF | F_AF | F_PF;
}

static pthread_mutex_t cx16_lock = PTHREAD_MUTEX_INITIALIZER;

// ---------------------------------------------------------------- execute

#define ALU_CASES(b) case b: case b + 1: case b + 2: case b + 3: case b + 4: case b + 5

static int exec(struct ictx *c, const struct insn *in) {
    struct cpu_state *cpu = c->cpu;
    uint64_t next = cpu->rip + in->len;
    int sz = in->opsz;
    uint64_t a, b, r, addr;

    switch (in->op) {
        ALU_CASES(0x00): ALU_CASES(0x08): ALU_CASES(0x10): ALU_CASES(0x18):
        ALU_CASES(0x20): ALU_CASES(0x28): ALU_CASES(0x30): ALU_CASES(0x38): {
            int op = (in->op >> 3) & 7, form = in->op & 7;
            if (!(form & 1))
                sz = 1;
            switch (form >> 1) {
                case 0:
                    alu_E(c, in, op, sz, rget(cpu, in, in->reg, sz));
                    break;
                case 1:
                    b = eget(c, in, sz);
                    a = rget(cpu, in, in->reg, sz);
                    r = alu(c, op, sz, a, b, c->cf);
                    if (op != ALU_CMP)
                        rset(cpu, in, in->reg, sz, r);
                    break;
                default:
                    a = rget(cpu, in, 0, sz);
                    r = alu(c, op, sz, a, (uint64_t) in->imm, c->cf);
                    if (op != ALU_CMP)
                        rset(cpu, in, 0, sz, r);
                    break;
            }
            break;
        }

        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            push64(c, cpu->regs[in->reg]);
            break;
        case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
            a = mrd(c, cpu->rsp, 8);
            cpu->rsp += 8;
            cpu->regs[in->reg] = a;
            break;

        case 0x63: // movsxd
            a = eget(c, in, 4);
            rset(cpu, in, in->reg, sz, sz == 8 ? (uint64_t) (int64_t) (int32_t) a : a);
            break;
        case 0x68: case 0x6a:
            push64(c, (uint64_t) in->imm);
            break;
        case 0x69: case 0x6b:
            a = eget(c, in, sz);
            r = imul_op(c, sz, a, (uint64_t) in->imm);
            rset(cpu, in, in->reg, sz, r);
            break;

        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
        case 0x180: case 0x181: case 0x182: case 0x183: case 0x184: case 0x185: case 0x186: case 0x187:
        case 0x188: case 0x189: case 0x18a: case 0x18b: case 0x18c: case 0x18d: case 0x18e: case 0x18f:
            if (cond(c, in->op & 0xf))
                next += (uint64_t) in->imm;
            break;

        case 0x80: case 0x81: case 0x83:
            if (in->op == 0x80)
                sz = 1;
            alu_E(c, in, in->reg & 7, sz, (uint64_t) in->imm);
            break;
        case 0x84: case 0x85:
            if (in->op == 0x84)
                sz = 1;
            a = eget(c, in, sz);
            alu(c, ALU_AND, sz, a, rget(cpu, in, in->reg, sz), 0);
            break;
        case 0x86: case 0x87:
            if (in->op == 0x86)
                sz = 1;
            b = rget(cpu, in, in->reg, sz);
            if (in->mod == 3) {
                a = rget(cpu, in, in->rm, sz);
                rset(cpu, in, in->rm, sz, b);
            } else {
                addr = ea(c, in);
                void *p = wptr(c, addr, sz);
                if (p == NULL) {
                    a = mrd(c, addr, sz);
                    mwr(c, addr, sz, b);
                } else {
                    do {
                        a = aload(p, sz);
                    } while (!acas(p, sz, a, b));
                }
            }
            rset(cpu, in, in->reg, sz, a);
            break;
        case 0x88:
            eset(c, in, 1, rget(cpu, in, in->reg, 1));
            break;
        case 0x89:
            eset(c, in, sz, rget(cpu, in, in->reg, sz));
            break;
        case 0x8a:
            rset(cpu, in, in->reg, 1, eget(c, in, 1));
            break;
        case 0x8b:
            rset(cpu, in, in->reg, sz, eget(c, in, sz));
            break;
        case 0x8d:
            if (in->mod == 3)
                return undefined_insn(c, in);
            rset(cpu, in, in->reg, sz, ea_noseg(c, in));
            break;
        case 0x8f:
            if ((in->reg & 7) != 0)
                return undefined_insn(c, in);
            a = mrd(c, cpu->rsp, 8);
            if (in->mod == 3) {
                cpu->rsp += 8;
                cpu->regs[in->rm] = a;
            } else {
                // the address is computed with rsp already incremented
                cpu->rsp += 8;
                addr = ea(c, in);
                cpu->rsp -= 8;
                mwr(c, addr, 8, a);
                cpu->rsp += 8;
            }
            break;

        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
            if (in->reg != 0) {
                a = rget(cpu, in, 0, sz);
                b = rget(cpu, in, in->reg, sz);
                rset(cpu, in, 0, sz, b);
                rset(cpu, in, in->reg, sz, a);
            }
            break;
        case 0x98:
            if (sz == 2) cpu->ax = (uint16_t) (int8_t) cpu->al;
            else if (sz == 4) cpu->rax = (uint32_t) (int16_t) cpu->ax;
            else cpu->rax = (uint64_t) (int64_t) (int32_t) cpu->eax;
            break;
        case 0x99:
            if (sz == 2) cpu->dx = (int16_t) cpu->ax < 0 ? 0xffff : 0;
            else if (sz == 4) cpu->rdx = (int32_t) cpu->eax < 0 ? 0xffffffffu : 0;
            else cpu->rdx = (int64_t) cpu->rax < 0 ? ~0ULL : 0;
            break;
        case 0x9b: // fwait
            break;
        case 0x9c:
            push64(c, flags_save(c));
            break;
        case 0x9d:
            a = mrd(c, cpu->rsp, 8);
            cpu->rsp += 8;
            flags_load(c, c->flags_other | ((uint32_t) a & (ARITH_FLAGS | F_DF)));
            break;
        case 0x9e: {
            uint32_t f = flags_save(c);
            f = (f & ~0xd5u) | (cpu->ah & 0xd5);
            flags_load(c, f);
            break;
        }
        case 0x9f:
            cpu->ah = (uint8_t) (flags_save(c) & 0xff);
            break;

        case 0xa0: case 0xa1: case 0xa2: case 0xa3:
            if (!(in->op & 1))
                sz = 1;
            addr = (uint64_t) in->imm + segbase(c, in);
            if (in->op < 0xa2)
                rset(cpu, in, 0, sz, mrd(c, addr, sz));
            else
                mwr(c, addr, sz, rget(cpu, in, 0, sz));
            break;

        case 0xa4: case 0xa5: case 0xa6: case 0xa7: case 0xaa: case 0xab:
        case 0xac: case 0xad: case 0xae: case 0xaf:
            return do_string(c, in, next);

        case 0xa8: case 0xa9:
            if (in->op == 0xa8)
                sz = 1;
            alu(c, ALU_AND, sz, rget(cpu, in, 0, sz), (uint64_t) in->imm, 0);
            break;

        case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7:
            rset(cpu, in, in->reg, 1, (uint64_t) in->imm);
            break;
        case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
            rset(cpu, in, in->reg, sz, (uint64_t) in->imm);
            break;

        case 0xc0: case 0xc1: case 0xd0: case 0xd1: case 0xd2: case 0xd3: {
            if (!(in->op & 1))
                sz = 1;
            unsigned cnt = in->op < 0xd0 ? (unsigned) in->imm : in->op < 0xd2 ? 1 : cpu->cl;
            int kind = in->reg & 7;
            if (in->mod == 3) {
                a = rget(cpu, in, in->rm, sz);
                r = shift_op(c, kind, sz, a, cnt);
                rset(cpu, in, in->rm, sz, r);
            } else {
                addr = ea(c, in);
                void *p = wptr(c, addr, sz);
                a = p ? ld(p, sz) : mrd(c, addr, sz);
                r = shift_op(c, kind, sz, a, cnt);
                if (p) st(p, sz, r); else mwr(c, addr, sz, r);
            }
            break;
        }

        case 0xc2: case 0xc3:
            a = mrd(c, cpu->rsp, 8);
            cpu->rsp += 8 + (in->op == 0xc2 ? (uint16_t) in->imm : 0);
            next = a;
            break;
        case 0xc6: case 0xc7:
            if ((in->reg & 7) != 0)
                return undefined_insn(c, in);
            eset(c, in, in->op == 0xc6 ? 1 : sz, (uint64_t) in->imm);
            break;
        case 0xc9:
            a = mrd(c, cpu->rbp, 8);
            cpu->rsp = cpu->rbp + 8;
            cpu->rbp = a;
            break;
        case 0xcc:
            cpu->rip = next;
            return INT_BREAKPOINT;

        case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf: {
            int err = exec_x87(c, in);
            if (err != INT_NONE)
                return err;
            break;
        }

        case 0xe0: case 0xe1: case 0xe2: {
            uint64_t cx = (in->pfx & P_ADDR32) ? (uint32_t) (cpu->rcx - 1) : cpu->rcx - 1;
            if (in->pfx & P_ADDR32) cpu->rcx = cx; else cpu->rcx = cx;
            bool take = cx != 0;
            if (in->op == 0xe0) take = take && c->zr != 0;
            if (in->op == 0xe1) take = take && c->zr == 0;
            if (take)
                next += (uint64_t) in->imm;
            break;
        }
        case 0xe3:
            if (((in->pfx & P_ADDR32) ? (uint32_t) cpu->rcx : cpu->rcx) == 0)
                next += (uint64_t) in->imm;
            break;
        case 0xe8:
            push64(c, next);
            next += (uint64_t) in->imm;
            break;
        case 0xe9: case 0xeb:
            next += (uint64_t) in->imm;
            break;

        case 0xf4:
            cpu->segfault_addr = 0;
            cpu->segfault_addr64 = 0;
            cpu->segfault_was_write = false;
            return INT_GPF;
        case 0xf5: c->cf ^= 1; break;
        case 0xf8: c->cf = 0; break;
        case 0xf9: c->cf = 1; break;
        case 0xfc: c->df = 0; break;
        case 0xfd: c->df = 1; break;

        case 0xf6: case 0xf7:
            if (in->op == 0xf6)
                sz = 1;
            switch (in->reg & 7) {
                case 0: case 1:
                    a = eget(c, in, sz);
                    alu(c, ALU_AND, sz, a, (uint64_t) in->imm, 0);
                    break;
                case 2:
                    RMW_E(c, in, sz, r = ~a & szmask(sz));
                    break;
                case 3:
                    RMW_E(c, in, sz, r = neg_op(c, sz, a));
                    break;
                case 4: // mul
                    b = eget(c, in, sz);
                    if (sz == 1) {
                        r = (uint64_t) cpu->al * (uint8_t) b;
                        cpu->ax = (uint16_t) r;
                        c->cf = c->of = (r >> 8) != 0;
                    } else if (sz == 8) {
                        unsigned __int128 full = (unsigned __int128) cpu->rax * b;
                        cpu->rax = (uint64_t) full;
                        cpu->rdx = (uint64_t) (full >> 64);
                        c->cf = c->of = cpu->rdx != 0;
                        r = cpu->rax;
                    } else {
                        uint64_t full = rget(cpu, in, 0, sz) * b;
                        uint64_t hi = (full >> (sz * 8)) & szmask(sz);
                        rset(cpu, in, 0, sz, full);
                        rset(cpu, in, 2, sz, hi);
                        c->cf = c->of = hi != 0;
                        r = full;
                    }
                    setszp(c, r, sz);
                    c->aux = 0;
                    c->undef = F_SF | F_ZF | F_AF | F_PF;
                    break;
                case 5: // imul
                    b = eget(c, in, sz);
                    if (sz == 1) {
                        int64_t full = (int64_t) (int8_t) cpu->al * (int8_t) b;
                        cpu->ax = (uint16_t) full;
                        c->cf = c->of = full != (int8_t) full;
                        r = (uint64_t) full;
                    } else if (sz == 8) {
                        __int128 full = (__int128) (int64_t) cpu->rax * (int64_t) b;
                        cpu->rax = (uint64_t) full;
                        cpu->rdx = (uint64_t) ((unsigned __int128) full >> 64);
                        c->cf = c->of = full != (__int128) (int64_t) cpu->rax;
                        r = cpu->rax;
                    } else {
                        int64_t full = sx(rget(cpu, in, 0, sz), sz) * sx(b, sz);
                        rset(cpu, in, 0, sz, (uint64_t) full);
                        rset(cpu, in, 2, sz, (uint64_t) full >> (sz * 8));
                        c->cf = c->of = full != sx((uint64_t) full, sz);
                        r = (uint64_t) full;
                    }
                    setszp(c, r, sz);
                    c->aux = 0;
                    c->undef = F_SF | F_ZF | F_AF | F_PF;
                    break;
                case 6: // div
                    b = eget(c, in, sz);
                    if (b == 0)
                        return INT_DIV;
                    if (sz == 1) {
                        unsigned q = cpu->ax / (uint8_t) b, rem = cpu->ax % (uint8_t) b;
                        if (q > 0xff)
                            return INT_DIV;
                        cpu->al = (uint8_t) q; cpu->ah = (uint8_t) rem;
                    } else if (sz == 8) {
                        unsigned __int128 n = ((unsigned __int128) cpu->rdx << 64) | cpu->rax;
                        if (cpu->rdx >= b)
                            return INT_DIV;
                        cpu->rax = (uint64_t) (n / b);
                        cpu->rdx = (uint64_t) (n % b);
                    } else {
                        uint64_t n = (rget(cpu, in, 2, sz) << (sz * 8)) | rget(cpu, in, 0, sz);
                        uint64_t q = n / b, rem = n % b;
                        if (q > szmask(sz))
                            return INT_DIV;
                        rset(cpu, in, 0, sz, q);
                        rset(cpu, in, 2, sz, rem);
                    }
                    c->undef = ARITH_FLAGS;
                    break;
                default: // idiv
                    b = eget(c, in, sz);
                    if (b == 0)
                        return INT_DIV;
                    if (sz == 1) {
                        int n = (int16_t) cpu->ax, dv = (int8_t) b;
                        int q = n / dv, rem = n % dv;
                        if (q != (int8_t) q)
                            return INT_DIV;
                        cpu->al = (uint8_t) q; cpu->ah = (uint8_t) rem;
                    } else if (sz == 8) {
                        __int128 n = (__int128) (((unsigned __int128) cpu->rdx << 64) | cpu->rax);
                        int64_t dv = (int64_t) b;
                        __int128 q = n / dv, rem = n % dv;
                        if (q != (__int128) (int64_t) q)
                            return INT_DIV;
                        cpu->rax = (uint64_t) q;
                        cpu->rdx = (uint64_t) rem;
                    } else {
                        int64_t n = (int64_t) ((rget(cpu, in, 2, sz) << (sz * 8)) | rget(cpu, in, 0, sz));
                        if (sz == 2)
                            n = (int32_t) n;
                        int64_t dv = sx(b, sz);
                        if (n == INT64_MIN && dv == -1)
                            return INT_DIV;
                        int64_t q = n / dv, rem = n % dv;
                        if (q != sx((uint64_t) q, sz))
                            return INT_DIV;
                        rset(cpu, in, 0, sz, (uint64_t) q);
                        rset(cpu, in, 2, sz, (uint64_t) rem);
                    }
                    c->undef = ARITH_FLAGS;
                    break;
            }
            break;

        case 0xfe: case 0xff:
            if (in->op == 0xfe)
                sz = 1;
            switch (in->reg & 7) {
                case 0:
                    RMW_E(c, in, sz, r = incdec(c, false, sz, a));
                    break;
                case 1:
                    RMW_E(c, in, sz, r = incdec(c, true, sz, a));
                    break;
                case 2:
                    if (sz == 1) return undefined_insn(c, in);
                    a = eget(c, in, 8);
                    push64(c, next);
                    next = a;
                    break;
                case 4:
                    if (sz == 1) return undefined_insn(c, in);
                    next = eget(c, in, 8);
                    break;
                case 6:
                    if (sz == 1) return undefined_insn(c, in);
                    a = eget(c, in, 8);
                    push64(c, a);
                    break;
                default:
                    return undefined_insn(c, in);
            }
            break;

        // ------------------------------------------------ 0F map
        case 0x105: // syscall
            cpu->rcx = next;
            cpu->r11 = flags_save(c);
            cpu->rip = next;
            return INT_SYSCALL;
        case 0x10b:
            return INT_UNDEFINED;
        case 0x10d: case 0x118: case 0x119: case 0x11a: case 0x11b: case 0x11c: case 0x11d: case 0x11e: case 0x11f:
            break; // prefetch, nop, endbr64
        case 0x131: {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t t = (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
            cpu->rax = (uint32_t) t;
            cpu->rdx = t >> 32;
            break;
        }
        case 0x140: case 0x141: case 0x142: case 0x143: case 0x144: case 0x145: case 0x146: case 0x147:
        case 0x148: case 0x149: case 0x14a: case 0x14b: case 0x14c: case 0x14d: case 0x14e: case 0x14f:
            a = eget(c, in, sz);
            if (cond(c, in->op & 0xf))
                rset(cpu, in, in->reg, sz, a);
            else if (sz == 4)
                cpu->regs[in->reg] = (uint32_t) cpu->regs[in->reg];
            break;
        case 0x190: case 0x191: case 0x192: case 0x193: case 0x194: case 0x195: case 0x196: case 0x197:
        case 0x198: case 0x199: case 0x19a: case 0x19b: case 0x19c: case 0x19d: case 0x19e: case 0x19f:
            eset(c, in, 1, cond(c, in->op & 0xf));
            break;
        case 0x1a2:
            do_cpuid(cpu);
            break;
        case 0x1a3: bitop(c, in, sz, 0, rget(cpu, in, in->reg, sz), true); break;
        case 0x1ab: bitop(c, in, sz, 1, rget(cpu, in, in->reg, sz), true); break;
        case 0x1b3: bitop(c, in, sz, 2, rget(cpu, in, in->reg, sz), true); break;
        case 0x1bb: bitop(c, in, sz, 3, rget(cpu, in, in->reg, sz), true); break;
        case 0x1ba:
            if ((in->reg & 7) < 4)
                return undefined_insn(c, in);
            bitop(c, in, sz, (in->reg & 7) - 4, (uint64_t) in->imm, false);
            break;
        case 0x1a4: case 0x1a5: case 0x1ac: case 0x1ad: {
            unsigned cnt = (in->op & 1) ? cpu->cl : (unsigned) in->imm;
            bool right = in->op >= 0x1ac;
            b = rget(cpu, in, in->reg, sz);
            if (in->mod == 3) {
                a = rget(cpu, in, in->rm, sz);
                r = shiftd_op(c, right, sz, a, b, cnt);
                rset(cpu, in, in->rm, sz, r);
            } else {
                addr = ea(c, in);
                void *p = wptr(c, addr, sz);
                a = p ? ld(p, sz) : mrd(c, addr, sz);
                r = shiftd_op(c, right, sz, a, b, cnt);
                if (p) st(p, sz, r); else mwr(c, addr, sz, r);
            }
            break;
        }
        case 0x1ae:
            if (in->mod == 3)
                break; // lfence, mfence, sfence
            addr = ea(c, in);
            switch (in->reg & 7) {
                case 0: fxsave(c, addr); break;
                case 1: fxrstor(c, addr); break;
                case 2: set_mxcsr(c, (uint32_t) mrd(c, addr, 4)); break;
                case 3: mwr(c, addr, 4, cpu->mxcsr); break;
                case 7: break; // clflush
                default: return undefined_insn(c, in);
            }
            break;
        case 0x1af:
            a = rget(cpu, in, in->reg, sz);
            b = eget(c, in, sz);
            rset(cpu, in, in->reg, sz, imul_op(c, sz, a, b));
            break;
        case 0x1b0: case 0x1b1: {
            if (in->op == 0x1b0)
                sz = 1;
            uint64_t acc = rget(cpu, in, 0, sz);
            b = rget(cpu, in, in->reg, sz);
            if (in->mod == 3) {
                a = rget(cpu, in, in->rm, sz);
                if (a == acc)
                    rset(cpu, in, in->rm, sz, b);
            } else {
                addr = ea(c, in);
                void *p = wptr(c, addr, sz);
                if (p == NULL) {
                    a = mrd(c, addr, sz);
                    mwr(c, addr, sz, a == acc ? b : a);
                } else if (in->pfx & P_LOCK) {
                    do {
                        a = aload(p, sz);
                    } while (a == acc && !acas(p, sz, a, b));
                } else {
                    a = ld(p, sz);
                    if (a == acc)
                        st(p, sz, b);
                }
            }
            alu(c, ALU_CMP, sz, acc, a, 0);
            if (a != acc)
                rset(cpu, in, 0, sz, a);
            break;
        }
        case 0x1b6: rset(cpu, in, in->reg, sz, eget(c, in, 1)); break;
        case 0x1b7: rset(cpu, in, in->reg, sz, eget(c, in, 2)); break;
        case 0x1be: rset(cpu, in, in->reg, sz, (uint64_t) (int64_t) (int8_t) eget(c, in, 1)); break;
        case 0x1bf: rset(cpu, in, in->reg, sz, (uint64_t) (int64_t) (int16_t) eget(c, in, 2)); break;
        case 0x1b8:
            if (!(in->pfx & P_F3))
                return undefined_insn(c, in);
            a = eget(c, in, sz);
            r = (uint64_t) __builtin_popcountll(a);
            rset(cpu, in, in->reg, sz, r);
            c->cf = c->of = 0; c->aux = 0; c->sn = 0; c->pr = 1;
            c->zr = a;
            break;
        case 0x1bc: case 0x1bd:
            a = eget(c, in, sz);
            if (a == 0) {
                c->zr = 0;
            } else {
                c->zr = 1;
                r = in->op == 0x1bc ? (uint64_t) __builtin_ctzll(a) : (uint64_t) (63 - __builtin_clzll(a));
                rset(cpu, in, in->reg, sz, r);
            }
            c->undef = F_CF | F_OF | F_SF | F_AF | F_PF;
            break;
        case 0x1c0: case 0x1c1:
            if (in->op == 0x1c0)
                sz = 1;
            b = rget(cpu, in, in->reg, sz);
            RMW_E(c, in, sz, r = alu(c, ALU_ADD, sz, a, b, 0));
            rset(cpu, in, in->reg, sz, a);
            if (in->mod == 3 && in->rm == in->reg)
                rset(cpu, in, in->rm, sz, r);
            break;
        case 0x1c3:
            if (in->mod == 3)
                return undefined_insn(c, in);
            mwr(c, ea(c, in), sz, rget(cpu, in, in->reg, sz));
            break;
        case 0x1c7: {
            if ((in->reg & 7) != 1 || in->mod == 3)
                return undefined_insn(c, in);
            addr = ea(c, in);
            int half = sz == 8 ? 8 : 4;
            wprobe(c, addr, half * 2);
            uint64_t m = szmask(half);
            void *p = wptr(c, addr, half * 2);
            if (p != NULL && ((uintptr_t) p & (half * 2 - 1)) == 0) {
                // a real atomic compare-and-swap: lock-free code (allocators,
                // garbage collectors) mixes this with plain loads and stores
                if (half == 8) {
                    unsigned __int128 want = ((unsigned __int128) cpu->rdx << 64) | cpu->rax;
                    unsigned __int128 repl = ((unsigned __int128) cpu->rcx << 64) | cpu->rbx;
                    unsigned __int128 was = __sync_val_compare_and_swap((unsigned __int128 *) p, want, repl);
                    if (was == want) {
                        c->zr = 0;
                    } else {
                        cpu->rax = (uint64_t) was;
                        cpu->rdx = (uint64_t) (was >> 64);
                        c->zr = 1;
                    }
                } else {
                    uint64_t want = ((cpu->rdx & m) << 32) | (cpu->rax & m);
                    uint64_t repl = ((cpu->rcx & m) << 32) | (cpu->rbx & m);
                    uint64_t was = __sync_val_compare_and_swap((uint64_t *) p, want, repl);
                    if (was == want) {
                        c->zr = 0;
                    } else {
                        rset(cpu, in, 0, 4, was & m);
                        rset(cpu, in, 2, 4, was >> 32);
                        c->zr = 1;
                    }
                }
                break;
            }
            // misaligned or across pages: atomic only against other such operations
            pthread_mutex_lock(&cx16_lock);
            uint64_t lo = mrd(c, addr, half), hi = mrd(c, addr + half, half);
            if (lo == (cpu->rax & m) && hi == (cpu->rdx & m)) {
                mwr(c, addr, half, cpu->rbx);
                mwr(c, addr + half, half, cpu->rcx);
                c->zr = 0;
            } else {
                rset(cpu, in, 0, half, lo);
                rset(cpu, in, 2, half, hi);
                c->zr = 1;
            }
            pthread_mutex_unlock(&cx16_lock);
            break;
        }
        case 0x1c8: case 0x1c9: case 0x1ca: case 0x1cb: case 0x1cc: case 0x1cd: case 0x1ce: case 0x1cf:
            if (sz == 8)
                cpu->regs[in->reg] = __builtin_bswap64(cpu->regs[in->reg]);
            else
                cpu->regs[in->reg] = __builtin_bswap32((uint32_t) cpu->regs[in->reg]);
            break;

        default:
            if (in->op >= 0x110 && in->op < 0x400) {
                int err = in->op < 0x200 ? exec_sse(c, in) : in->op < 0x300 ? exec_0f38(c, in) : exec_0f3a(c, in);
                if (err != INT_NONE)
                    return err;
                break;
            }
            return undefined_insn(c, in);
    }
    cpu->rip = next;
    return INT_NONE;
}

static int stats_on;
static __thread unsigned long stats_generic[1024];
#include "emu/interp64_fast.h"

// ---------------------------------------------------------------- decode

enum { OF_M = 1, OF_I8 = 2, OF_IZ = 4, OF_IV = 8, OF_I16 = 16, OF_MOFFS = 32, OF_REL32 = 64 };
static uint8_t optab[1024];

__attribute__((constructor)) static void optab_init(void) {
    for (int op = 0; op < 0x40; op++) {
        if ((op & 7) < 4) optab[op] = OF_M;
        else if ((op & 7) == 4) optab[op] = OF_I8;
        else if ((op & 7) == 5) optab[op] = OF_IZ;
    }
    optab[0x63] = OF_M; optab[0x68] = OF_IZ; optab[0x69] = OF_M | OF_IZ;
    optab[0x6a] = OF_I8; optab[0x6b] = OF_M | OF_I8;
    for (int op = 0x70; op < 0x80; op++) optab[op] = OF_I8;
    optab[0x80] = OF_M | OF_I8; optab[0x81] = OF_M | OF_IZ; optab[0x83] = OF_M | OF_I8;
    for (int op = 0x84; op < 0x90; op++) optab[op] = OF_M;
    for (int op = 0xa0; op < 0xa4; op++) optab[op] = OF_MOFFS;
    optab[0xa8] = OF_I8; optab[0xa9] = OF_IZ;
    for (int op = 0xb0; op < 0xb8; op++) optab[op] = OF_I8;
    for (int op = 0xb8; op < 0xc0; op++) optab[op] = OF_IV;
    optab[0xc0] = optab[0xc1] = OF_M | OF_I8;
    optab[0xc2] = OF_I16;
    optab[0xc6] = OF_M | OF_I8; optab[0xc7] = OF_M | OF_IZ;
    optab[0xcd] = OF_I8;
    for (int op = 0xd0; op < 0xd4; op++) optab[op] = OF_M;
    for (int op = 0xd8; op < 0xe0; op++) optab[op] = OF_M;
    for (int op = 0xe0; op < 0xe4; op++) optab[op] = OF_I8;
    optab[0xe8] = optab[0xe9] = OF_REL32;
    optab[0xeb] = OF_I8;
    optab[0xf6] = optab[0xf7] = optab[0xfe] = optab[0xff] = OF_M;

    for (int op = 0x100; op < 0x200; op++) optab[op] = OF_M;
    static const uint8_t no_modrm[] = {0x05, 0x06, 0x08, 0x09, 0x0b, 0x30, 0x31, 0x32, 0x34, 0x35, 0x77, 0xa0, 0xa1, 0xa2, 0xa8, 0xa9};
    for (unsigned i = 0; i < sizeof(no_modrm); i++) optab[0x100 + no_modrm[i]] = 0;
    for (int op = 0x180; op < 0x190; op++) optab[op] = OF_REL32;
    for (int op = 0x1c8; op < 0x1d0; op++) optab[op] = 0;
    static const uint8_t modrm_i8[] = {0x70, 0x71, 0x72, 0x73, 0xa4, 0xac, 0xba, 0xc2, 0xc4, 0xc5, 0xc6};
    for (unsigned i = 0; i < sizeof(modrm_i8); i++) optab[0x100 + modrm_i8[i]] = OF_M | OF_I8;
    // 0F 38: modrm; 0F 3A: modrm and an immediate byte
    for (int op = 0x200; op < 0x300; op++) optab[op] = OF_M;
    for (int op = 0x300; op < 0x400; op++) optab[op] = OF_M | OF_I8;
}

// Decode one instruction from p (at least 32 readable bytes). Returns its length.
static unsigned decode(const uint8_t *p, uint64_t rip, struct insn *in) {
    const uint8_t *s = p;
    unsigned pfx = 0, rex = 0;
    for (int n = 0; n < 14; n++) {
        uint8_t b = *p;
        if (b == 0x66) pfx |= P_66;
        else if (b == 0x67) pfx |= P_ADDR32;
        else if (b == 0xf2) pfx = (pfx & ~P_F3) | P_F2;
        else if (b == 0xf3) pfx = (pfx & ~P_F2) | P_F3;
        else if (b == 0xf0) pfx |= P_LOCK;
        else if (b == 0x64) pfx = (pfx & ~P_GS) | P_FS;
        else if (b == 0x65) pfx = (pfx & ~P_FS) | P_GS;
        else if (b == 0x2e || b == 0x36 || b == 0x3e || b == 0x26) ;
        else break;
        p++;
    }
    if ((*p & 0xf0) == 0x40) {
        rex = *p++;
        pfx |= P_REX;
    }
    unsigned op = *p++;
    if (op == 0x0f) {
        op = 0x100 | *p++;
        if (op == 0x138)
            op = 0x200 | *p++;
        else if (op == 0x13a)
            op = 0x300 | *p++;
    }
    in->op = (uint16_t) op;
    in->pfx = (uint8_t) pfx;
    in->opsz = (rex & 8) ? 8 : (pfx & P_66) ? 2 : 4;
    in->mod = 3;
    in->reg = 0;
    in->rm = 0;
    in->base = in->index = RNONE;
    in->scale = 0;
    in->disp = 0;
    in->imm = 0;
    bool riprel = false;
    unsigned fl = optab[op];

    if (fl & OF_M) {
        uint8_t modrm = *p++;
        unsigned mod = modrm >> 6, rm = modrm & 7;
        in->mod = (uint8_t) mod;
        in->reg = (uint8_t) (((modrm >> 3) & 7) | ((rex & 4) << 1));
        if (mod == 3) {
            in->rm = (uint8_t) (rm | ((rex & 1) << 3));
        } else {
            if (rm == 4) {
                uint8_t sib = *p++;
                unsigned idx = ((sib >> 3) & 7) | ((rex & 2) << 2);
                unsigned base = sib & 7;
                in->scale = sib >> 6;
                if (idx != 4)
                    in->index = (uint8_t) idx;
                if (base == 5 && mod == 0) {
                    int32_t d; memcpy(&d, p, 4); p += 4;
                    in->disp = d;
                } else {
                    in->base = (uint8_t) (base | ((rex & 1) << 3));
                }
            } else if (rm == 5 && mod == 0) {
                int32_t d; memcpy(&d, p, 4); p += 4;
                in->disp = d;
                riprel = true;
            } else {
                in->base = (uint8_t) (rm | ((rex & 1) << 3));
            }
            if (mod == 1) {
                in->disp = (int8_t) *p++;
            } else if (mod == 2) {
                int32_t d; memcpy(&d, p, 4); p += 4;
                in->disp = d;
            }
        }
        if (mod != 3 && (pfx & (P_FS | P_GS)) && !(pfx & P_ADDR32) && op != 0x8d) {
            // fold the segment base into a free base or index slot
            uint8_t segreg = (pfx & P_FS) ? RFS : RGS;
            if (in->index == RNONE) {
                in->index = segreg;
                in->scale = 0;
                in->pfx &= (uint8_t) ~(P_FS | P_GS);
            } else if (in->base == RNONE) {
                in->base = segreg;
                in->pfx &= (uint8_t) ~(P_FS | P_GS);
            }
        }
        if (op == 0xf6 && (in->reg & 7) < 2) fl |= OF_I8;
        if (op == 0xf7 && (in->reg & 7) < 2) fl |= OF_IZ;
    } else {
        // register in the low opcode bits (push, pop, xchg, mov imm, bswap)
        in->reg = (uint8_t) ((op & 7) | ((rex & 1) << 3));
    }

    if (fl & OF_I8) {
        in->imm = (int8_t) *p++;
    } else if (fl & OF_IZ) {
        if (in->opsz == 2) {
            int16_t v; memcpy(&v, p, 2); p += 2;
            in->imm = v;
        } else {
            int32_t v; memcpy(&v, p, 4); p += 4;
            in->imm = v;
        }
    } else if (fl & OF_REL32) {
        int32_t v; memcpy(&v, p, 4); p += 4;
        in->imm = v;
    } else if (fl & OF_IV) {
        if (in->opsz == 8) {
            memcpy(&in->imm, p, 8); p += 8;
        } else if (in->opsz == 2) {
            uint16_t v; memcpy(&v, p, 2); p += 2;
            in->imm = v;
        } else {
            uint32_t v; memcpy(&v, p, 4); p += 4;
            in->imm = v;
        }
    } else if (fl & OF_I16) {
        uint16_t v; memcpy(&v, p, 2); p += 2;
        in->imm = v;
    } else if (fl & OF_MOFFS) {
        if (pfx & P_ADDR32) {
            uint32_t v; memcpy(&v, p, 4); p += 4;
            in->imm = v;
        } else {
            memcpy(&in->imm, p, 8); p += 8;
        }
    }
    unsigned len = (unsigned) (p - s);
    in->len = (uint8_t) len;
    if (riprel)
        in->disp += (int64_t) (rip + len);
    in->fn = pick_handler(in);
    return len;
}

static int step(struct ictx *c) {
    struct cpu_state *cpu = c->cpu;
    uint64_t rip = cpu->rip;
    struct insn in[2];
    const uint8_t *p = xlat_r(c, rip);
    unsigned off = rip & (PAGE_SIZE - 1);
    if (likely(off <= PAGE_SIZE - 32)) {
        decode(p, rip, &in[0]);
    } else {
        uint8_t buf[48] = {0};
        unsigned avail = PAGE_SIZE - off;
        memcpy(buf, p, avail);
        uint64_t next_page = rip + avail;
        const uint8_t *p2 = __tlb_read_ptr(c->tlb, next_page);
        if (p2 != NULL) {
            memcpy(buf + avail, p2, 16);
            avail += 16;
        }
        if (decode(buf, rip, &in[0]) > avail)
            fault(c, next_page, false);
    }
    in[1].fn = h_end;
    if (unlikely(c->log))
        c->last = in[0];
    return in[0].fn(c, in, cpu);
}

// ---------------------------------------------------------------- decoded block cache
//
// Instructions are decoded once into blocks (straight-line runs) and kept in
// a per-thread cache. A block is valid while the generation numbers of the
// pages it was decoded from are unchanged; memory.c bumps a page's generation
// (through asbestos_invalidate_page) whenever the page is unmapped or replaced,
// and xlat_w_miss bumps it on every write to a page that holds code.

#define BC_HASH_BITS 15
// two entries per bucket: BC_HASH gives the first, the other is BC_HASH ^ 1
#define BC_HASH(rip) ((uint32_t) (((rip) * 0x9e3779b97f4a7c15ull) >> (64 - BC_HASH_BITS)))
#define BC_ARENA (1u << 18)
#define BLOCK_MAX 48 // instructions, plus one end marker
struct bent {
    uint64_t addr;
    uint32_t first;
    uint16_t n;
    uint16_t gi0, gi1;
    uint32_t gen0, gen1;
};
struct bcache {
    uint64_t space_id;
    uint32_t used;
    struct bent *hash;
    struct insn *arena;
};

static pthread_key_t bcache_key;
static pthread_once_t bcache_once = PTHREAD_ONCE_INIT;
static void bcache_free(void *p) {
    struct bcache *bc = p;
    if (bc == NULL)
        return;
    free(bc->hash);
    free(bc->arena);
    free(bc);
}
static void bcache_key_init(void) {
    pthread_key_create(&bcache_key, bcache_free);
}
static void bcache_flush(struct bcache *bc) {
    for (size_t i = 0; i < (1u << BC_HASH_BITS); i++)
        bc->hash[i].addr = ~0ULL;
    bc->used = 0;
}
static struct bcache *bcache_get(struct space *sp) {
    static __thread struct bcache *bc;
    if (unlikely(bc == NULL)) {
        pthread_once(&bcache_once, bcache_key_init);
        bc = calloc(1, sizeof(*bc));
        bc->hash = malloc(sizeof(struct bent) << BC_HASH_BITS);
        bc->arena = malloc(sizeof(struct insn) * BC_ARENA);
        bc->space_id = sp->id;
        bcache_flush(bc);
        pthread_setspecific(bcache_key, bc);
    }
    if (unlikely(bc->space_id != sp->id)) {
        bc->space_id = sp->id;
        bcache_flush(bc);
    }
    return bc;
}

static forceinline bool ends_block(const struct insn *in) {
    switch (in->op) {
        case 0xc2: case 0xc3: case 0xe8: case 0xe9: case 0xeb: case 0xf4: case 0xcc:
        case 0x105: case 0x10b:
            return true;
        case 0xff:
            return (in->reg & 7) == 2 || (in->reg & 7) == 4;
    }
    return in->op >= 0x400;
}

static int stats_on;
static __thread unsigned long stats_builds, stats_flushes, stats_genmiss, stats_collide;

// Remember that this page holds decoded instructions. The first time, writable
// TLB entries for it have to go: this thread's directly, other threads' by
// making every TLB of the address space refill (mmu->changes).
static void block_mark_code(struct ictx *c, struct space *sp, page_t page) {
    if (likely(space_has_code(sp, PAGE_ADDR(page))) || page >= MEM_PAGES)
        return;
    uint8_t **slot = &sp->code[page / CODE_LEAF_PAGES];
    uint8_t *leaf = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (leaf == NULL) {
        uint8_t *fresh = calloc(CODE_LEAF_PAGES / 8, 1);
        if (__atomic_compare_exchange_n(slot, &leaf, fresh, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            leaf = fresh;
        else
            free(fresh); // another thread got there first; leaf is theirs
    }
    __atomic_fetch_or(&leaf[(page % CODE_LEAF_PAGES) >> 3], (uint8_t) (1u << (page & 7)), __ATOMIC_SEQ_CST);
    // a writable entry for this page may be cached in this or another TLB
    __atomic_fetch_add(&c->cpu->mmu->changes, 1, __ATOMIC_SEQ_CST);
    tlb_flush(c->tlb);
}

static __attribute__((noinline)) struct bent *block_build(struct ictx *c, struct bcache *bc, struct space *sp, uint64_t rip) {
    stats_builds++;
    if (bc->used + BLOCK_MAX + 1 > BC_ARENA) {
        bcache_flush(bc);
        stats_flushes++;
    }
    // take the bucket's free entry, or else its second one, moving the first
    // there so the entry replaced is the one that was not used most recently
    struct bent *e = &bc->hash[BC_HASH(rip)];
    if (e->addr != ~0ULL && e->addr != rip) {
        struct bent *other = &bc->hash[BC_HASH(rip) ^ 1];
        if (other->addr != rip)
            *other = *e;
    }
    e->addr = ~0ULL;
    const uint8_t *p = xlat_r(c, rip);
    page_t page = PAGE((addr_t) rip);
    uint16_t gi0 = (uint16_t) GEN_INDEX(page), gi1 = gi0;
    uint32_t gen0 = __atomic_load_n(&sp->gens[gi0], __ATOMIC_RELAXED), gen1 = gen0;
    struct insn *out = bc->arena + bc->used;
    unsigned n = 0;
    uint64_t addr = rip;
    for (;;) {
        unsigned off = addr & (PAGE_SIZE - 1);
        if (unlikely(off > PAGE_SIZE - 32)) {
            // too close to the end of the page to decode in place: this
            // instruction gets a block of its own
            if (n > 0)
                break;
            uint8_t buf[48] = {0};
            unsigned avail = PAGE_SIZE - off;
            memcpy(buf, p, avail);
            uint64_t next_page = addr + avail;
            gi1 = (uint16_t) GEN_INDEX(page + 1);
            gen1 = __atomic_load_n(&sp->gens[gi1], __ATOMIC_RELAXED);
            const uint8_t *p2 = __tlb_read_ptr(c->tlb, next_page);
            if (p2 != NULL) {
                memcpy(buf + avail, p2, 16);
                avail += 16;
            }
            if (decode(buf, addr, &out[0]) > avail)
                fault(c, next_page, false);
            n = 1;
            break;
        }
        unsigned len = decode(p + (addr - rip), addr, &out[n]);
        addr += len;
        if (ends_block(&out[n++]) || n == BLOCK_MAX)
            break;
    }
    block_mark_code(c, sp, page);
    if (gi1 != gi0)
        block_mark_code(c, sp, page + 1);
    out[n].fn = h_end;
    e->first = bc->used;
    e->n = (uint16_t) n;
    e->gi0 = gi0; e->gi1 = gi1;
    e->gen0 = gen0; e->gen1 = gen1;
    e->addr = rip;
    bc->used += n + 1;
    return e;
}

static int stats_on;
static __thread unsigned long stats_count, stats_last_count;
static __thread struct timespec stats_last;
static __attribute__((noinline)) void stats_tick(unsigned n) {
    stats_count += n;
    if (stats_count - stats_last_count < 50000000)
        return;
    struct timespec now;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    double dt = (double) (now.tv_sec - stats_last.tv_sec) + (double) (now.tv_nsec - stats_last.tv_nsec) / 1e9;
    fprintf(stderr, "interp64: %lu M instructions, last %.0f M at %.1f M/s, %lu blocks decoded (%lu page changed, %lu hash collisions), %lu cache flushes\n", stats_count / 1000000,
            (double) (stats_count - stats_last_count) / 1e6, (double) (stats_count - stats_last_count) / 1e6 / dt, stats_builds, stats_genmiss, stats_collide, stats_flushes);
    // the instructions that went through the generic path most often
    fprintf(stderr, "  generic:");
    for (int k = 0; k < 14; k++) {
        int best = 0;
        for (int i = 1; i < 1024; i++)
            if (stats_generic[i] > stats_generic[best]) best = i;
        if (stats_generic[best] == 0)
            break;
        fprintf(stderr, " %x:%luk", best, stats_generic[best] / 1000);
        stats_generic[best] = 0;
    }
    fprintf(stderr, "\n");
    memset(stats_generic, 0, sizeof(stats_generic));
    stats_last = now;
    stats_last_count = stats_count;
}
__attribute__((constructor)) static void stats_init(void) {
    stats_on = getenv("ISH_STATS") != NULL;
}

static __attribute__((noinline)) int run(struct ictx *c) {
    struct cpu_state *cpu = c->cpu;
    struct space *sp = (struct space *) cpu->mmu->asbestos;
    struct bcache *bc = bcache_get(sp);
    unsigned executed = 0;
    for (unsigned blocks = 1; ; blocks++) {
        uint64_t rip = cpu->rip;
        struct bent *e = &bc->hash[BC_HASH(rip)];
        if (unlikely(e->addr != rip)) {
            struct bent *e2 = &bc->hash[BC_HASH(rip) ^ 1];
            if (e2->addr == rip)
                e = e2;
        }
        if (unlikely(e->addr != rip || e->gen0 != sp->gens[e->gi0] || e->gen1 != sp->gens[e->gi1])) {
            if (stats_on) { if (e->addr == rip) stats_genmiss++; else if (e->addr != ~0ULL) stats_collide++; }
            e = block_build(c, bc, sp, rip);
        }
        const struct insn *in = bc->arena + e->first;
        int interrupt = in->fn(c, in, cpu);
        if (unlikely(interrupt != INT_NONE))
            return interrupt;
        if (unlikely((blocks & 0xff) == 0)) {
            if (__atomic_exchange_n(cpu->poked_ptr, false, __ATOMIC_SEQ_CST))
                return INT_TIMER;
            if ((blocks & 0x3fff) == 0)
                return INT_TIMER;
        }
        if (unlikely(stats_on)) {
            executed += e->n;
            if (executed >= 0x10000) {
                stats_tick(executed);
                executed = 0;
            }
        }
    }
}

static void ctx_enter(struct ictx *c, struct cpu_state *cpu, struct tlb *tlb) {
    c->cpu = cpu;
    c->tlb = tlb;
    c->space = (struct space *) cpu->mmu->asbestos;
    c->single = c->log = c->log_overflow = false;
    c->nlog = 0;
    c->undef = 0;
    cpu->zero_reg = 0;
    flags_load(c, cpu->eflags);
    sync_rounding(cpu);
}
static void ctx_leave(struct ictx *c) {
    struct cpu_state *cpu = c->cpu;
    cpu->eflags = flags_save(c);
    // the kernel layer reads flags through these
    cpu->cf = cpu->cf_bit;
    cpu->of = cpu->of_bit;
    cpu->flags_res = 0;
}

static int interp_run(struct cpu_state *cpu, struct tlb *tlb, bool single) {
    struct ictx c;
    ctx_enter(&c, cpu, tlb);
    c.single = single;
    int interrupt;
    if (_setjmp(c.fault)) {
        interrupt = INT_GPF;
    } else if (single) {
        interrupt = step(&c);
    } else {
        interrupt = run(&c);
    }
    ctx_leave(&c);
    return interrupt;
}

#if ISH_UNICORN_REF
#include "emu/interp64_check.h"
#endif

int cpu_run_to_interrupt(struct cpu_state *cpu, struct tlb *tlb) {
    if (cpu->poked_ptr == NULL)
        cpu->poked_ptr = &cpu->_poked;
    tlb_refresh(tlb, cpu->mmu);
    int interrupt;
#if ISH_UNICORN_REF
    static int mode = -1; // 0 interpreter, 1 unicorn, 2 lockstep check
    if (mode < 0) {
        const char *e = getenv("ISH_ENGINE");
        mode = e == NULL ? 0 : strcmp(e, "unicorn") == 0 ? 1 : strcmp(e, "check") == 0 ? 2 : 0;
    }
    if (mode == 1)
        return unicorn64_run(cpu, false);
    if (mode == 2)
        return check_run(cpu, tlb);
#endif
    if (__atomic_exchange_n(cpu->poked_ptr, false, __ATOMIC_SEQ_CST))
        interrupt = INT_TIMER;
    else
        interrupt = interp_run(cpu, tlb, cpu->tf);
    if (interrupt == INT_NONE)
        interrupt = INT_DEBUG; // single step
    cpu->trapno = interrupt;
    return interrupt;
}

void cpu_poke(struct cpu_state *cpu) {
    __atomic_store_n(cpu->poked_ptr, true, __ATOMIC_SEQ_CST);
}

// memory.c's hooks for a per-address-space code cache. The struct behind the
// pointer is ours (struct space), not asbestos's.
struct asbestos *asbestos_new(struct mmu *mmu) {
    (void) mmu;
    static uint64_t next_id = 1;
    struct space *sp = calloc(1, sizeof(struct space));
    sp->id = __atomic_fetch_add(&next_id, 1, __ATOMIC_SEQ_CST);
    return (struct asbestos *) sp;
}
void asbestos_free(struct asbestos *asbestos) {
    struct space *sp = (struct space *) asbestos;
    for (size_t i = 0; i < sizeof(sp->code) / sizeof(sp->code[0]); i++)
        free(sp->code[i]);
    free(sp);
}
void asbestos_invalidate_page(struct asbestos *asbestos, page_t page) {
    struct space *sp = (struct space *) asbestos;
    __atomic_fetch_add(&sp->gens[GEN_INDEX(page)], 1, __ATOMIC_RELAXED);
}
