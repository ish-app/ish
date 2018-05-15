#include "emu/cpu.h"
#include "emu/cpuid.h"
#include "emu/modrm.h"
#include "emu/regid.h"

// TODO get rid of these
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtautological-constant-out-of-range-compare"

#define DECLARE_LOCALS \
    dword_t addr_offset = 0; \
    dword_t saved_ip = cpu->eip; \
    struct regptr modrm_regptr, modrm_base; \
    dword_t addr = 0; \
    \
    union xmm_reg xmm_src; \
    union xmm_reg xmm_dst; \
    \
    extFloat80_t ftmp;

#define RETURN(thing) return (thing)

#define UNDEFINED { cpu->eip = saved_ip; return INT_UNDEFINED; }

static bool modrm_compute(struct cpu_state *cpu, struct tlb *tlb, addr_t *addr_out,
        struct modrm *modrm, struct regptr *modrm_regptr, struct regptr *modrm_base);
#define READMODRM \
    if (!modrm_compute(cpu, tlb, &addr, &modrm, &modrm_regptr, &modrm_base)) { \
        cpu->segfault_addr = cpu->eip; \
        cpu->eip = saved_ip; \
        return INT_GPF; \
    }
#define READADDR READIMM_(addr_offset, 32); addr += addr_offset

#define _READIMM(name,size) \
    name = mem_read(cpu->eip, size); \
    cpu->eip += size/8

#define TRACEIP() TRACE("%d %08x\t", current->pid, cpu->eip);

#define SEG_GS() addr += cpu->tls_ptr

#define sz(x) sz_##x
/* #define sz_ OP_SIZE */
/* #define sz_8 8 */
/* #define sz_16 16 */
/* #define sz_32 32 */
/* #define sz_64 64 */
/* #define sz_80 80 */
/* #define sz_128 128 */
#define twice(x) glue(twice_, x)
#define twice_8 16
#define twice_16 32
#define twice_32 64

// types for different sizes
#define ty(x) ty_##x
#define ty_8 uint8_t
#define ty_16 uint16_t
#define ty_32 uint32_t
#define ty_64 uint64_t
#define ty_128 union xmm_reg

#define mem_read_ts(addr, type, size) ({ \
    type val; \
    if (!tlb_read(tlb, addr, &val, size/8)) { \
        cpu->eip = saved_ip; \
        cpu->segfault_addr = addr; \
        return INT_GPF; \
    } \
    val; \
})
#define mem_write_ts(addr, val, type, size) ({ \
    type _val = val; \
    if (!tlb_write(tlb, addr, &_val, size/8)) { \
        cpu->eip = saved_ip; \
        cpu->segfault_addr = addr; \
        return INT_GPF; \
    } \
})
#define mem_read(addr, size) mem_read_ts(addr, ty(size), size)
#define mem_write(addr, val, size) mem_write_ts(addr, val, ty(size), size)

#define get(what) get_##what()
#define set(what, to) set_##what(to)
#define is_memory(what) is_memory_##what

#define REGISTER(regptr, size) (*(ty(size) *) (((char *) cpu) + (regptr).glue3(reg, size, _id)))

#define get_modrm_reg() REGISTER(modrm_regptr, OP_SIZE)
#define set_modrm_reg(to) REGISTER(modrm_regptr, OP_SIZE) = to
#define get_modrm_reg8() REGISTER(modrm_regptr, 8)
#define set_modrm_reg8(to) REGISTER(modrm_regptr, 8) = to
#define set_modrm_reg32(to) REGISTER(modrm_regptr, 32) = to
#define get_modrm_reg128() REGISTER(modrm_regptr, 128)
#define set_modrm_reg128(to) REGISTER(modrm_regptr, 128) = to
#define sz_modrm_reg OP_SIZE
#define sz_modrm_reg8 8
#define is_memory_modrm_reg 0
#define is_memory_modrm_reg8 0
#define is_memory_modrm_reg128 0

#define is_memory_modrm_val (modrm.type != modrm_reg)
#define get_modrm_val_s(size) \
    (modrm.type == modrm_reg ? \
     REGISTER(modrm_base, size) : \
     mem_read(addr, size))
#define get_modrm_val() get_modrm_val_s(OP_SIZE)
#define get_modrm_val8() get_modrm_val_s(8)
#define get_modrm_val16() get_modrm_val_s(16)
#define get_modrm_val128() get_modrm_val_s(128)
#define sz_modrm_val OP_SIZE
#define sz_modrm_val8 8
#define sz_modrm_val16 16
#define is_memory_modrm_val128 0

#define set_modrm_val_s(to, size) \
    if (modrm.type == modrm_reg) { \
        REGISTER(modrm_base, size) = to; \
    } else { \
        mem_write(addr, to, size); \
    }(void)0
#define set_modrm_val(to) set_modrm_val_s(to, OP_SIZE)
#define set_modrm_val8(to) set_modrm_val_s(to, 8)
#define set_modrm_val32(to) set_modrm_val_s(to, 32)
#define set_modrm_val16(to) set_modrm_val_s(to, 16)
#define set_modrm_val128(to) set_modrm_val_s(to, 128)

#define get_imm() (/*(uint(OP_SIZE))*/ imm)

#define get_mem_addr() mem_read(addr, OP_SIZE)
#define get_mem_addr8() mem_read(addr, 8)
#define get_mem_addr16() mem_read(addr, 16)
#define get_mem_addr32() mem_read(addr, 32)
#define get_mem_addr64() mem_read(addr, 64)
#define set_mem_addr(to) mem_write(addr, to, OP_SIZE)
#define set_mem_addr8(to) mem_write(addr, to, 8)
#define set_mem_addr16(to) mem_write(addr, to, 16)
#define set_mem_addr32(to) mem_write(addr, to, 32)
#define set_mem_addr64(to) mem_write(addr, to, 64)
#define sz_mem_addr OP_SIZE
#define sz_mem_addr8 8
#define sz_mem_addr16 16
#define sz_mem_addr32 32
#define sz_mem_addr64 64

// DEFINE ALL THE MACROS
#define get_oax() cpu->oax
#define get_obx() cpu->obx
#define get_ocx() cpu->ocx
#define get_odx() cpu->odx
#define get_osi() cpu->osi
#define get_odi() cpu->odi
#define get_obp() cpu->obp
#define get_osp() cpu->osp
#define get_eax() cpu->eax
#define get_ebx() cpu->ebx
#define get_ecx() cpu->ecx
#define get_edx() cpu->edx
#define get_esi() cpu->esi
#define get_edi() cpu->edi
#define get_ebp() cpu->ebp
#define get_esp() cpu->esp
#define get_eip() cpu->eip
#define get_eflags() cpu->eflags
#define get_ax() cpu->ax
#define get_bx() cpu->bx
#define get_cx() cpu->cx
#define get_dx() cpu->dx
#define get_si() cpu->si
#define get_di() cpu->di
#define get_bp() cpu->bp
#define get_sp() cpu->sp
#define get_al() cpu->al
#define get_bl() cpu->bl
#define get_cl() cpu->cl
#define get_dl() cpu->dl
#define get_ah() cpu->ah
#define get_bh() cpu->bh
#define get_ch() cpu->ch
#define get_dh() cpu->dh
#define get_gs() cpu->gs
#define set_oax(to) cpu->oax = to
#define set_obx(to) cpu->obx = to
#define set_ocx(to) cpu->ocx = to
#define set_odx(to) cpu->odx = to
#define set_osi(to) cpu->osi = to
#define set_odi(to) cpu->odi = to
#define set_obp(to) cpu->obp = to
#define set_osp(to) cpu->osp = to
#define set_eax(to) cpu->eax = to
#define set_ebx(to) cpu->ebx = to
#define set_ecx(to) cpu->ecx = to
#define set_edx(to) cpu->edx = to
#define set_esi(to) cpu->esi = to
#define set_edi(to) cpu->edi = to
#define set_ebp(to) cpu->ebp = to
#define set_esp(to) cpu->esp = to
#define set_eip(to) cpu->eip = to
#define set_eflags(to) cpu->eflags = to
#define set_ax(to) cpu->ax = to
#define set_bx(to) cpu->bx = to
#define set_cx(to) cpu->cx = to
#define set_dx(to) cpu->dx = to
#define set_si(to) cpu->si = to
#define set_di(to) cpu->di = to
#define set_bp(to) cpu->bp = to
#define set_sp(to) cpu->sp = to
#define set_al(to) cpu->al = to
#define set_bl(to) cpu->bl = to
#define set_cl(to) cpu->cl = to
#define set_dl(to) cpu->dl = to
#define set_ah(to) cpu->ah = to
#define set_bh(to) cpu->bh = to
#define set_ch(to) cpu->ch = to
#define set_dh(to) cpu->dh = to
#define set_gs(to) cpu->gs = to
#define sz_oax OP_SIZE
#define sz_obx OP_SIZE
#define sz_ocx OP_SIZE
#define sz_odx OP_SIZE
#define sz_osi OP_SIZE
#define sz_odi OP_SIZE
#define sz_obp OP_SIZE
#define sz_osp OP_SIZE
#define sz_eax 32
#define sz_ebx 32
#define sz_ecx 32
#define sz_edx 32
#define sz_esi 32
#define sz_edi 32
#define sz_ebp 32
#define sz_esp 32
#define sz_eip 32
#define sz_eflags cpu->eflags
#define sz_ax 16
#define sz_bx 16
#define sz_cx 16
#define sz_dx 16
#define sz_si 16
#define sz_di 16
#define sz_bp 16
#define sz_sp 16
#define sz_al 8
#define sz_bl 8
#define sz_cl 8
#define sz_dl 8
#define sz_ah 8
#define sz_bh 8
#define sz_ch 8
#define sz_dh 8
#define sz_gs 8

#define get_0() 0
#define get_1() 1

// only used by lea
#define get_addr(size) addr

// INSTRUCTION MACROS
// if an instruction accesses memory, it should do that before it modifies
// registers, so segfault recovery only needs to save IP.

// takes any unsigned integer and casts it to signed of the same size

#define unsigned_overflow(what, a, b, res, z) ({ \
    int ov = __builtin_##what##_overflow((uint(z)) (a), (uint(z)) (b), (uint(z) *) &res); \
    res = (sint(z)) res; ov; \
})
#define signed_overflow(what, a, b, res, z) ({ \
    int ov = __builtin_##what##_overflow((sint(z)) (a), (sint(z)) (b), (sint(z) *) &res); \
    res = (sint(z)) res; ov; \
})

#define MOV(src, dst) \
    set(dst, get(src))
#define MOVSX(src, dst) \
    set(dst, (uint(sz(dst))) (sint(sz(src))) get(src))

#define XCHG(src, dst) do { \
    dword_t tmp = get(src); \
    set(src, get(dst)); \
    set(dst, tmp); \
} while (0)

#define PUSH(thing) \
    mem_write(cpu->osp - OP_SIZE/8, get(thing), OP_SIZE); \
    cpu->osp -= OP_SIZE/8
#define POP(thing) \
    set(thing, mem_read(cpu->osp, OP_SIZE)); \
    cpu->osp += OP_SIZE/8

#define INT(code) \
    return (uint8_t) get(code)

// math

#define SETRESFLAGS cpu->zf_res = cpu->sf_res = cpu->pf_res = 1
#define SETRES_SIZE(result, z) \
    cpu->res = (int32_t) (sint(z)) (result); SETRESFLAGS
#define SETRES(result) SETRES_SIZE(get(result), sz(result))
    // ^ sign extend result so SF is correct
#define ZEROAF cpu->af = cpu->af_ops = 0
#define SETAF(a, b) \
    cpu->op1 = get(a); cpu->op2 = get(b); cpu->af_ops = 1

#define TEST(src, dst) \
    SETRES_SIZE(get(dst) & get(src), sz(dst)); \
    cpu->cf = cpu->of = cpu->af = cpu->af_ops = 0

#define ADD(src, dst) \
    SETAF(src, dst); \
    cpu->cf = unsigned_overflow(add, get(dst), get(src), cpu->res, sz(dst)); \
    cpu->of = signed_overflow(add, get(dst), get(src), cpu->res, sz(dst)); \
    set(dst, cpu->res); SETRESFLAGS

#define ADC(src, dst) \
    SETAF(src, dst); \
    cpu->of = signed_overflow(add, get(dst), get(src) + cpu->cf, cpu->res, sz(dst)) \
        || (cpu->cf && get(src) == ((uint(sz(dst))) -1) / 2); \
    cpu->cf = unsigned_overflow(add, get(dst), get(src) + cpu->cf, cpu->res, sz(dst)) \
        || (cpu->cf && get(src) == (uint(sz(dst))) -1); \
    set(dst, cpu->res); SETRESFLAGS

#define SBB(src, dst) \
    SETAF(src, dst); \
    cpu->of = signed_overflow(sub, get(dst), get(src) + cpu->cf, cpu->res, sz(dst)) \
        || (cpu->cf && get(src) == ((uint(sz(dst))) -1) / 2); \
    cpu->cf = unsigned_overflow(sub, get(dst), get(src) + cpu->cf, cpu->res, sz(dst)) \
        || (cpu->cf && get(src) == (uint(sz(dst))) -1); \
    set(dst, cpu->res); SETRESFLAGS

#define OR(src, dst) \
    set(dst, get(dst) | get(src)); \
    cpu->cf = cpu->of = cpu->af = cpu->af_ops = 0; SETRES(dst)

#define AND(src, dst) \
    set(dst, get(dst) & get(src)); \
    cpu->cf = cpu->of = cpu->af = cpu->af_ops = 0; SETRES(dst)

#define SUB(src, dst) \
    SETAF(src, dst); \
    cpu->of = signed_overflow(sub, get(dst), get(src), cpu->res, sz(dst)); \
    cpu->cf = unsigned_overflow(sub, get(dst), get(src), cpu->res, sz(dst)); \
    set(dst, cpu->res); SETRESFLAGS

#define XOR(src, dst) \
    set(dst, get(dst) ^ get(src)); \
    cpu->cf = cpu->of = cpu->af = cpu->af_ops = 0; SETRES(dst)

#define CMP(src, dst) \
    SETAF(src, dst); \
    cpu->cf = unsigned_overflow(sub, get(dst), get(src), cpu->res, sz(dst)); \
    cpu->of = signed_overflow(sub, get(dst), get(src), cpu->res, sz(dst)); \
    SETRESFLAGS

#define INC(val) do { \
    int tmp = cpu->cf; \
    ADD(1, val); \
    cpu->cf = tmp; \
} while (0)
#define DEC(val) do { \
    int tmp = cpu->cf; \
    SUB(1, val); \
    cpu->cf = tmp; \
} while (0)

#define MUL18(val) cpu->ax = cpu->al * val
#define MUL1(val) do { \
    uint64_t tmp = cpu->oax * (uint64_t) get(val); \
    cpu->oax = tmp; cpu->odx = tmp >> sz(val); \
    cpu->cf = cpu->of = (tmp != (uint32_t) tmp); ZEROAF; \
    cpu->zf = cpu->sf = cpu->pf = cpu->zf_res = cpu->sf_res = cpu->pf_res = 0; \
} while (0)
#define IMUL1(val) do { \
    int64_t tmp = (int64_t) (sint(sz(val))) cpu->oax * (sint(sz(val))) get(val); \
    cpu->oax = tmp; cpu->odx = tmp >> sz(val); \
    cpu->cf = cpu->of = (tmp != (int32_t) tmp); \
    cpu->zf = cpu->sf = cpu->pf = cpu->zf_res = cpu->sf_res = cpu->pf_res = 0; \
} while (0)
#define MUL2(val, reg) \
    cpu->cf = cpu->of = unsigned_overflow(mul, reg, val, cpu->res, sz(val)); \
    set(reg, cpu->res); SETRESFLAGS
#define IMUL2(val, reg) \
    cpu->cf = cpu->of = signed_overflow(mul, get(reg), get(val), cpu->res, sz(val)); \
    set(reg, cpu->res); SETRESFLAGS
#define MUL3(imm, src, dst) \
    cpu->cf = cpu->of = unsigned_overflow(mul, get(src), get(imm), cpu->res, sz(dst)); \
    set(dst, cpu->res); \
    cpu->pf_res = 1; cpu->zf = cpu->sf = cpu->zf_res = cpu->sf_res = 0
#define IMUL3(imm, src, dst) \
    cpu->cf = cpu->of = signed_overflow(mul, get(src), get(imm), cpu->res, sz(dst)); \
    set(dst, cpu->res); \
    cpu->pf_res = 1; cpu->zf = cpu->sf = cpu->zf_res = cpu->sf_res = 0

#define DIV(reg, val, rem) do { \
    if (get(val) == 0) return INT_DIV; \
    uint(twice(OP_SIZE)) dividend = get(reg) | ((uint(twice(OP_SIZE))) get(rem) << OP_SIZE); \
    set(rem, dividend % get(val)); \
    set(reg, dividend / get(val)); \
} while (0)

#define IDIV(reg, val, rem) do { \
    if (get(val) == 0) return INT_DIV; \
    sint(twice(OP_SIZE)) dividend = get(reg) | ((sint(twice(OP_SIZE))) get(rem) << OP_SIZE); \
    set(rem, dividend % get(val)); \
    set(reg, dividend / get(val)); \
} while (0)

// TODO this is probably wrong in some subtle way
#define HALF_OP_SIZE glue(HALF_, OP_SIZE)
#define HALF_16 8
#define HALF_32 16
#define CVT \
    cpu->odx = cpu->oax & (1 << (OP_SIZE - 1)) ? (uint(OP_SIZE)) -1 : 0
#define CVTE \
    REG_VAL(cpu, REG_ID(eax), HALF_OP_SIZE) = (sint(OP_SIZE)) REG_VAL(cpu, REG_ID(ax), OP_SIZE)

#define CALL(loc) PUSH(eip); JMP(loc)
#define CALL_REL(offset) PUSH(eip); JMP_REL(offset)

#define ROL(count, val) \
    if (get(count) % sz(val) != 0) { \
        int cnt = get(count) % sz(val); \
        /* the compiler miraculously turns this into a rol instruction with optimizations on */\
        set(val, get(val) << cnt | get(val) >> (sz(val) - cnt)); \
        cpu->cf = get(val) & 1; \
        if (cnt == 1) { cpu->of = cpu->cf ^ (get(val) >> (OP_SIZE - 1)); } \
    }
#define ROR(count, val) \
    if (get(count) % sz(val) != 0) { \
        int cnt = get(count) % sz(val); \
        set(val, get(val) >> cnt | get(val) << (sz(val) - cnt)); \
        cpu->cf = get(val) >> (OP_SIZE - 1); \
        if (cnt == 1) { cpu->of = cpu->cf ^ (get(val) & 1); } \
    }
#define SHL(count, val) \
    if (get(count) % sz(val) != 0) { \
        int cnt = get(count) % sz(val); \
        cpu->cf = (get(val) << (cnt - 1)) >> (sz(val) - 1); \
        cpu->of = cpu->cf ^ (get(val) >> (sz(val) - 1)); \
        set(val, get(val) << cnt); SETRES(val); ZEROAF; \
    }
#define SHR(count, val) \
    if (get(count) % sz(val) != 0) { \
        int cnt = get(count) % sz(val); \
        cpu->cf = (get(val) >> (cnt - 1)) & 1; \
        cpu->of = get(val) >> (sz(val) - 1); \
        set(val, get(val) >> cnt); SETRES(val); ZEROAF; \
    }
#define SAR(count, val) \
    if (get(count) % sz(val) != 0) { \
        int cnt = get(count) % sz(val); \
        cpu->cf = (get(val) >> (cnt - 1)) & 1; cpu->of = 0; \
        set(val, ((sint(sz(val))) get(val)) >> cnt); SETRES(val); ZEROAF; \
    }

#define SHRD(count, extra, dst) \
    if (get(count) % sz(dst) != 0) { \
        int cnt = get(count) % sz(dst); \
        cpu->cf = (get(dst) >> (cnt - 1)) & 1; \
        cpu->res = get(dst) >> cnt | get(extra) << (sz(dst) - cnt); \
        set(dst, cpu->res); \
        SETRESFLAGS; \
    }

#define SHLD(count, extra, dst) \
    if (get(count) % sz(dst) != 0) { \
        int cnt = get(count) % sz(dst); \
        cpu->res = get(dst) << cnt | get(extra) >> (sz(dst) - cnt); \
        set(dst, cpu->res); \
        SETRESFLAGS; \
    }

#define NOT(val) \
    set(val, ~get(val)) // TODO flags
#define NEG(val) \
    SETAF(0, val); \
    cpu->of = signed_overflow(sub, 0, get(val), cpu->res, sz(val)); \
    cpu->cf = unsigned_overflow(sub, 0, get(val), cpu->res, sz(val)); \
    set(val, cpu->res); SETRESFLAGS; break; \

#define GRP38(val) \
    switch (modrm.opcode) { \
        case 0: \
        case 1: TRACE("test imm"); \
                READIMM8; TEST(imm, val); break; \
        case 2: TRACE("not"); return INT_UNDEFINED; \
        case 3: TRACE("neg"); return INT_UNDEFINED; \
        case 4: TRACE("mul"); return INT_UNDEFINED; \
        case 5: TRACE("imul"); return INT_UNDEFINED; \
        case 6: TRACE("div"); \
                DIV(cpu->al, modrm_val8, cpu->ah); break; \
        case 7: TRACE("idiv"); \
                IDIV(oax, modrm_val, odx); break; \
        default: TRACE("undefined"); return INT_UNDEFINED; \
    }

// bits

#define get_bit(bit, val) \
    ((is_memory(val) ? \
      mem_read(addr + get(bit) / sz(val) * (sz(val)/8), sz(val)) : \
      get(val)) & (1 << (get(bit) % sz(val)))) ? 1 : 0

#define msk(bit, val) (1 << (get(bit) % sz(val)))

#define BT(bit, val) \
    cpu->cf = get_bit(bit, val);

#define BTC(bit, val) \
    BT(bit, val); \
    set(val, get(val) ^ msk(bit, val))

#define BTS(bit, val) \
    BT(bit, val); \
    set(val, get(val) | msk(bit, val))

#define BTR(bit, val) \
    BT(bit, val); \
    set(val, get(val) & ~msk(bit, val))

#define BSF(src, dst) \
    cpu->zf = get(src) == 0; \
    cpu->zf_res = 0; \
    if (!cpu->zf) set(dst, __builtin_ctz(get(src)))

#define BSR(src, dst) \
    cpu->zf = get(src) == 0; \
    cpu->zf_res = 0; \
    if (!cpu->zf) set(dst, sz(dst) - __builtin_clz(get(src)))

// string instructions

#define get_mem_si() mem_read(cpu->osi, OP_SIZE)
#define get_mem_di() mem_read(cpu->odi, OP_SIZE)
#define get_mem_si8() mem_read(cpu->osi, 8)
#define get_mem_di8() mem_read(cpu->odi, 8)
#define sz_mem_si OP_SIZE
#define sz_mem_di OP_SIZE
#define sz_mem_si8 8
#define sz_mem_di8 8

#define BUMP_SI(size) \
    if (!cpu->df) \
        cpu->esi += size/8; \
    else \
        cpu->esi -= size/8
#define BUMP_DI(size) \
    if (!cpu->df) \
        cpu->edi += size/8; \
    else \
        cpu->edi -= size/8
#define BUMP_SI_DI(size) \
    BUMP_SI(size); BUMP_DI(size)

#define MOVS(z) \
    mem_write(cpu->edi, mem_read(cpu->esi, z), z); \
    BUMP_SI_DI(z)

#define STOS(z) \
    mem_write(cpu->edi, REG_VAL(cpu, REG_ID(eax), z), z); \
    BUMP_DI(z)

#define LODS(z) \
    REG_VAL(cpu, REG_ID(eax), z) = mem_read(cpu->esi, z); \
    BUMP_SI(z)

// found an alternative to al, see above, needs polishing
#define SCAS(z) \
    CMP(al, mem_di##z); \
    BUMP_DI(z)

#define CMPS(z) \
    CMP(mem_di##z, mem_si##z); \
    BUMP_SI_DI(z)

#define REP(OP) \
    while (cpu->ocx != 0) { \
        OP; \
        cpu->ocx--; \
    }

#define REPNZ(OP) \
    while (cpu->ocx != 0) { \
        OP; \
        cpu->ocx--; \
        if (ZF) break; \
    }

#define REPZ(OP) \
    while (cpu->ocx != 0) { \
        OP; \
        cpu->ocx--; \
        if (!ZF) break; \
    }

#define CMPXCHG(src, dst) \
    CMP(oax, dst); \
    if (E) { \
        MOV(src, dst); \
    } else \
        MOV(dst, oax)

#define XADD(src, dst) \
    XCHG(src, dst); \
    ADD(src, dst)

#define BSWAP(dst) \
    set(dst, __builtin_bswap32(get(dst)))

// condition codes
#define E ZF
#define B CF
#define BE (CF | ZF)
#define L (SF ^ OF)
#define LE (L | ZF)
#define O OF
#define P PF
#define S SF

// control transfer

#define FIX_EIP \
    if (OP_SIZE == 16) \
        cpu->eip &= 0xffff

#define JMP(loc) cpu->eip = get(loc); FIX_EIP;
#define JMP_REL(offset) cpu->eip += get(offset); FIX_EIP;
#define J_REL(cond, offset) \
    if (cond) { \
        cpu->eip += get(offset); FIX_EIP; \
    }
#define JN_REL(cond, offset) \
    if (!cond) { \
        cpu->eip += get(offset); FIX_EIP; \
    }
#define JCXZ_REL(offset) J_REL(cpu->ocx == 0, offset)

#define RET_NEAR() POP(eip); FIX_EIP
#define RET_NEAR_IMM(imm) RET_NEAR(); cpu->osp += (uint16_t) get(imm)

#define SET(cond, val) \
    set(val, (cond ? 1 : 0))

#define CMOV(cond, dst, src) \
    if (cond) MOV(dst, src)

#define POPF() \
    POP(eflags); \
    expand_flags(cpu)

#define PUSHF() \
    collapse_flags(cpu); \
    PUSH(eflags)

#define STD cpu->df = 1
#define CLD cpu->df = 0

#define AH_FLAG_MASK 0b11010101
#define SAHF \
    cpu->eflags &= 0xffffff00 | ~AH_FLAG_MASK; \
    cpu->eflags |= cpu->ah & AH_FLAG_MASK; \
    expand_flags(cpu)

#define RDTSC \
    imm = rdtsc(); \
    cpu->eax = imm & 0xffffffff; \
    cpu->edx = imm >> 32

#define CPUID() \
    do_cpuid(&cpu->eax, &cpu->ebx, &cpu->ecx, &cpu->edx)

#include "emu/interp/sse.h"
#include "emu/interp/fpu.h"

// ok now include the decoding function
#define DECODER_RET int
#define DECODER_NAME cpu_step
#define DECODER_ARGS struct cpu_state *cpu, struct tlb *tlb
#define DECODER_PASS_ARGS cpu, tlb

#define OP_SIZE 32
#include "emu/decode.h"
#undef OP_SIZE
#define OP_SIZE 16
#include "emu/decode.h"
#undef OP_SIZE

// reads a modrm and maybe sib byte, computes the address, and adds it to
// *addr_out, returns false if segfault while reading the bytes
static bool modrm_compute(struct cpu_state *cpu, struct tlb *tlb, addr_t *addr_out,
        struct modrm *modrm, struct regptr *modrm_regptr, struct regptr *modrm_base) {
    if (!modrm_decode32(&cpu->eip, tlb, modrm))
        return false;
    *modrm_regptr = regptr_from_reg(modrm->reg);
    *modrm_base = regptr_from_reg(modrm->base);
    if (modrm->type == modrm_reg)
        return true;

    if (modrm->base != reg_none)
        *addr_out += REGISTER(*modrm_base, 32);
    *addr_out += modrm->offset;
    if (modrm->type == modrm_mem_si) {
        struct regptr index_reg = regptr_from_reg(modrm->index);
        *addr_out += REGISTER(index_reg, 32) << modrm->shift;
    }
    return true;
}

flatten __no_instrument void cpu_run(struct cpu_state *cpu) {
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

/* int log_override = 0; */
