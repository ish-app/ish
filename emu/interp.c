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
    float80 ftmp;

#define FINISH \
    return -1 // everything is ok.

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

#define RESTORE_IP cpu->eip = saved_ip
#define _READIMM(name,size) \
    name = mem_read(cpu->eip, size); \
    cpu->eip += size/8

#define TRACEIP() TRACE("%d %08x\t", current->pid, cpu->eip);

#define SEG_GS() addr += cpu->tls_ptr

// this is a completely insane way to turn empty into OP_SIZE and any other size into itself
#define sz(x) sz_##x
#define sz_ OP_SIZE
#define sz_8 8
#define sz_16 16
#define sz_32 32
#define sz_64 64
#define sz_80 80
#define sz_128 128
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

#define get(what, size) get_##what(sz(size))
#define set(what, to, size) set_##what(to, sz(size))
#define is_memory(what) is_memory_##what

#define REGISTER(regptr, size) (*(ty(size) *) (((char *) cpu) + (regptr).reg##size##_id))

#define get_modrm_reg(size) REGISTER(modrm_regptr, size)
#define set_modrm_reg(to, size) REGISTER(modrm_regptr, size) = to
#define is_memory_modrm_reg 0

#define is_memory_modrm_val (modrm.type != modrm_reg)
#define get_modrm_val(size) \
    (modrm.type == modrm_reg ? \
     REGISTER(modrm_base, size) : \
     mem_read(addr, size))

#define set_modrm_val(to, size) \
    if (modrm.type == modrm_reg) { \
        REGISTER(modrm_base, size) = to; \
    } else { \
        mem_write(addr, to, size); \
    }(void)0

#define get_imm(size) ((uint(size)) imm)

#define get_mem_addr(size) mem_read(addr, size)
#define set_mem_addr(to, size) mem_write(addr, to, size)

#define get_mem_si(size) mem_read(cpu->esi, size)
#define set_mem_si(size) mem_write(cpu->esi, size)
#define get_mem_di(size) mem_read(cpu->edi, size)
#define set_mem_di(size) mem_write(cpu->esi, size)

// DEFINE ALL THE MACROS
#define get_reg_a(size) ((uint(size)) cpu->eax)
#define get_reg_b(size) ((uint(size)) cpu->ebx)
#define get_reg_c(size) ((uint(size)) cpu->ecx)
#define get_reg_d(size) ((uint(size)) cpu->edx)
#define get_reg_si(size) ((uint(size)) cpu->esi)
#define get_reg_di(size) ((uint(size)) cpu->edi)
#define get_reg_bp(size) ((uint(size)) cpu->ebp)
#define get_reg_sp(size) ((uint(size)) cpu->esp)
#define get_eip(size) cpu->eip
#define get_eflags(size) cpu->eflags
#define get_gs(size) cpu->gs
#define set_reg_a(to, size) *(uint(size) *) &cpu->eax = to
#define set_reg_b(to, size) *(uint(size) *) &cpu->ebx = to
#define set_reg_c(to, size) *(uint(size) *) &cpu->ecx = to
#define set_reg_d(to, size) *(uint(size) *) &cpu->edx = to
#define set_reg_si(to, size) *(uint(size) *) &cpu->esi = to
#define set_reg_di(to, size) *(uint(size) *) &cpu->edi = to
#define set_reg_bp(to, size) *(uint(size) *) &cpu->ebp = to
#define set_reg_sp(to, size) *(uint(size) *) &cpu->esp = to
#define set_eip(to, size) cpu->eip = to
#define set_eflags(to, size) cpu->eflags = to
#define set_gs(to, size) cpu->gs = to

#define get_0(size) 0
#define get_1(size) 1

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

#define MOV(src, dst,z) \
    set(dst, get(src,z),z)
#define MOVZX(src, dst, zs, zd) \
    set(dst, get(src,zs),zd)
#define MOVSX(src, dst, zs, zd) \
    set(dst, (uint(sz(zd))) (sint(sz(zs))) get(src,zs),zd)

#define XCHG(src, dst,z) do { \
    dword_t tmp = get(src,z); \
    set(src, get(dst,z),z); \
    set(dst, tmp,z); \
} while (0)

#define PUSH(thing,z) \
    mem_write(cpu->esp - OP_SIZE/8, get(thing,z),z); \
    cpu->esp -= OP_SIZE/8
#define POP(thing,z) \
    set(thing, mem_read(cpu->esp, z),z); \
    cpu->esp += OP_SIZE/8

#define INT(code) \
    return get(code,8)

// math

#define SETRESFLAGS cpu->zf_res = cpu->sf_res = cpu->pf_res = 1
#define SETRES_RAW(result,z)
#define SETRES(result,z) \
    cpu->res = (int32_t) (sint(z)) (result); SETRESFLAGS
    // ^ sign extend result so SF is correct
#define ZEROAF cpu->af = cpu->af_ops = 0
#define SETAF(a, b,z) \
    cpu->op1 = get(a,z); cpu->op2 = get(b,z); cpu->af_ops = 1

#define TEST(src, dst,z) \
    SETRES(get(dst,z) & get(src,z),z); \
    cpu->cf = cpu->of = cpu->af = cpu->af_ops = 0

#define ADD(src, dst,z) \
    SETAF(src, dst,z); \
    cpu->cf = unsigned_overflow(add, get(dst,z), get(src,z), cpu->res,z); \
    cpu->of = signed_overflow(add, get(dst,z), get(src,z), cpu->res,z); \
    set(dst, cpu->res,z); SETRESFLAGS

#define ADC(src, dst,z) \
    SETAF(src, dst,z); \
    cpu->of = signed_overflow(add, get(dst,z), get(src,z) + cpu->cf, cpu->res,z) \
        || (cpu->cf && get(src,z) == ((uint(z)) -1) / 2); \
    cpu->cf = unsigned_overflow(add, get(dst,z), get(src,z) + cpu->cf, cpu->res,z) \
        || (cpu->cf && get(src,z) == (uint(z)) -1); \
    set(dst, cpu->res,z); SETRESFLAGS

#define SBB(src, dst,z) \
    SETAF(src, dst,z); \
    cpu->of = signed_overflow(sub, get(dst,z), get(src,z) + cpu->cf, cpu->res,z) \
        || (cpu->cf && get(src,z) == ((uint(z)) -1) / 2); \
    cpu->cf = unsigned_overflow(sub, get(dst,z), get(src,z) + cpu->cf, cpu->res,z) \
        || (cpu->cf && get(src,z) == (uint(z)) -1); \
    set(dst, cpu->res,z); SETRESFLAGS

#define OR(src, dst,z) \
    set(dst, get(dst,z) | get(src,z),z); \
    cpu->cf = cpu->of = cpu->af = cpu->af_ops = 0; SETRES(get(dst,z),z)

#define AND(src, dst,z) \
    set(dst, get(dst,z) & get(src,z),z); \
    cpu->cf = cpu->of = cpu->af = cpu->af_ops = 0; SETRES(get(dst,z),z)

#define SUB(src, dst,z) \
    SETAF(src, dst,z); \
    cpu->of = signed_overflow(sub, get(dst,z), get(src,z), cpu->res,z); \
    cpu->cf = unsigned_overflow(sub, get(dst,z), get(src,z), cpu->res,z); \
    set(dst, cpu->res,z); SETRESFLAGS

#define XOR(src, dst,z) \
    set(dst, get(dst,z) ^ get(src,z),z); \
    cpu->cf = cpu->of = cpu->af = cpu->af_ops = 0; SETRES(get(dst,z),z)

#define CMP(src, dst,z) \
    SETAF(src, dst,z); \
    cpu->cf = unsigned_overflow(sub, get(dst,z), get(src,z), cpu->res,z); \
    cpu->of = signed_overflow(sub, get(dst,z), get(src,z), cpu->res,z); \
    SETRESFLAGS

#define INC(val,z) do { \
    int tmp = cpu->cf; \
    ADD(1, val,z); \
    cpu->cf = tmp; \
} while (0)
#define DEC(val,z) do { \
    int tmp = cpu->cf; \
    SUB(1, val,z); \
    cpu->cf = tmp; \
} while (0)

#define MUL18(val) cpu->ax = cpu->al * val
#define MUL1(val,z) do { \
    uint64_t tmp = get(reg_a,z) * (uint64_t) get(val,z); \
    set(reg_a, tmp,z); set(reg_d, tmp >> z,z);; \
    cpu->cf = cpu->of = (tmp != (uint32_t) tmp); ZEROAF; \
    cpu->zf = cpu->sf = cpu->pf = cpu->zf_res = cpu->sf_res = cpu->pf_res = 0; \
} while (0)
#define IMUL1(val,z) do { \
    int64_t tmp = (int64_t) (sint(z)) get(reg_a,z) * (sint(z)) get(val,z); \
    set(reg_a, tmp,z); set(reg_d, tmp >> z,z); \
    cpu->cf = cpu->of = (tmp != (int32_t) tmp); \
    cpu->zf = cpu->sf = cpu->pf = cpu->zf_res = cpu->sf_res = cpu->pf_res = 0; \
} while (0)
#define IMUL2(val, reg,z) \
    cpu->cf = cpu->of = signed_overflow(mul, get(reg,z), get(val,z), cpu->res,z); \
    set(reg, cpu->res,z); SETRESFLAGS
#define IMUL3(imm, src, dst,z) \
    cpu->cf = cpu->of = signed_overflow(mul, get(src,z), get(imm,z), cpu->res,z); \
    set(dst, cpu->res,z); \
    cpu->pf_res = 1; cpu->zf = cpu->sf = cpu->zf_res = cpu->sf_res = 0

#define DIV(val,z) do { \
    if (get(val,z) == 0) return INT_DIV; \
    uint(twice(z)) dividend = get(reg_a,z) | ((uint(twice(z))) get(reg_d,z) << z); \
    set(reg_d, dividend % get(val,z),z); \
    set(reg_a, dividend / get(val,z),z); \
} while (0)

#define IDIV(val,z) do { \
    if (get(val,z) == 0) return INT_DIV; \
    sint(twice(z)) dividend = get(reg_a,z) | ((sint(twice(z))) get(reg_d,z) << z); \
    set(reg_d, dividend % get(val,z),z); \
    set(reg_a, dividend / get(val,z),z); \
} while (0)

// TODO this is probably wrong in some subtle way
#define HALF_OP_SIZE glue(HALF_, OP_SIZE)
#define HALF_16 8
#define HALF_32 16
#define CVT \
    set(reg_d, get(reg_a,oz) & (1 << (oz - 1)) ? (uint(oz)) -1 : 0, oz)
#define CVTE \
    REG_VAL(cpu, REG_ID(eax), HALF_OP_SIZE) = (sint(OP_SIZE)) REG_VAL(cpu, REG_ID(ax), OP_SIZE)

#define CALL(loc) PUSH(eip,oz); JMP(loc)
#define CALL_REL(offset) PUSH(eip,oz); JMP_REL(offset)

#define ROL(count, val,z) \
    if (get(count,8) % z != 0) { \
        int cnt = get(count,8) % z; \
        /* the compiler miraculously turns this into a rol instruction with optimizations on */\
        set(val, get(val,z) << cnt | get(val,z) >> (z - cnt),z); \
        cpu->cf = get(val,z) & 1; \
        if (cnt == 1) { cpu->of = cpu->cf ^ (get(val,z) >> (OP_SIZE - 1)); } \
    }
#define ROR(count, val,z) \
    if (get(count,8) % z != 0) { \
        int cnt = get(count,8) % z; \
        set(val, get(val,z) >> cnt | get(val,z) << (z - cnt),z); \
        cpu->cf = get(val,z) >> (OP_SIZE - 1); \
        if (cnt == 1) { cpu->of = cpu->cf ^ (get(val,z) & 1); } \
    }
#define SHL(count, val,z) \
    if (get(count,8) % z != 0) { \
        int cnt = get(count,8) % z; \
        cpu->cf = (get(val,z) << (cnt - 1)) >> (z - 1); \
        cpu->of = cpu->cf ^ (get(val,z) >> (z - 1)); \
        set(val, get(val,z) << cnt,z); SETRES(get(val,z),z); ZEROAF; \
    }
#define SHR(count, val,z) \
    if (get(count,8) % z != 0) { \
        int cnt = get(count,8) % z; \
        cpu->cf = (get(val,z) >> (cnt - 1)) & 1; \
        cpu->of = get(val,z) >> (z - 1); \
        set(val, get(val,z) >> cnt,z); SETRES(get(val,z),z); ZEROAF; \
    }
#define SAR(count, val,z) \
    if (get(count,8) % z != 0) { \
        int cnt = get(count,8) % z; \
        cpu->cf = (get(val,z) >> (cnt - 1)) & 1; cpu->of = 0; \
        set(val, ((sint(z)) get(val,z)) >> cnt,z); SETRES(get(val,z),z); ZEROAF; \
    }

#define SHRD(count, extra, dst,z) \
    if (get(count,8) % z != 0) { \
        int cnt = get(count,8) % z; \
        cpu->cf = (get(dst,z) >> (cnt - 1)) & 1; \
        cpu->res = get(dst,z) >> cnt | get(extra,z) << (z - cnt); \
        set(dst, cpu->res,z); \
        SETRESFLAGS; \
    }

#define RCR(count, val,z) UNDEFINED
#define RCL(count, val,z) UNDEFINED

#define SHLD(count, extra, dst,z) \
    if (get(count,8) % z != 0) { \
        int cnt = get(count,8) % z; \
        cpu->res = get(dst,z) << cnt | get(extra,z) >> (z - cnt); \
        set(dst, cpu->res,z); \
        SETRESFLAGS; \
    }

#define NOT(val,z) \
    set(val, ~get(val,z),z) // TODO flags
#define NEG(val,z) \
    SETAF(0, val,z); \
    cpu->of = signed_overflow(sub, 0, get(val,z), cpu->res,z); \
    cpu->cf = unsigned_overflow(sub, 0, get(val,z), cpu->res,z); \
    set(val, cpu->res,z); SETRESFLAGS; break; \

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

#define get_bit(bit, val,z) \
    ((is_memory(val) ? \
      mem_read(addr + get(bit,z) / z * (z/8), z) : \
      get(val,z)) & (1 << (get(bit,z) % z))) ? 1 : 0

#define msk(bit,z) (1 << (get(bit,z) % z))

#define BT(bit, val,z) \
    cpu->cf = get_bit(bit, val,z);

#define BTC(bit, val,z) \
    BT(bit, val,z); \
    set(val, get(val,z) ^ msk(bit,z),z)

#define BTS(bit, val,z) \
    BT(bit, val,z); \
    set(val, get(val,z) | msk(bit,z),z)

#define BTR(bit, val,z) \
    BT(bit, val,z); \
    set(val, get(val,z) & ~msk(bit,z),z)

#define BSF(src, dst,z) \
    cpu->zf = get(src,z) == 0; \
    cpu->zf_res = 0; \
    if (!cpu->zf) set(dst, __builtin_ctz(get(src,z)),z)

#define BSR(src, dst,z) \
    cpu->zf = get(src,z) == 0; \
    cpu->zf_res = 0; \
    if (!cpu->zf) set(dst, z - __builtin_clz(get(src,z)),z)

// string instructions

#define BUMP_SI(size) \
    if (!cpu->df) \
        cpu->esi += sz(size)/8; \
    else \
        cpu->esi -= sz(size)/8
#define BUMP_DI(size) \
    if (!cpu->df) \
        cpu->edi += sz(size)/8; \
    else \
        cpu->edi -= sz(size)/8
#define BUMP_SI_DI(size) \
    BUMP_SI(size); BUMP_DI(size)

#define str_movs(z) \
    mem_write(cpu->edi, mem_read(cpu->esi, z), z); \
    BUMP_SI_DI(z)
#define str_stos(z) \
    mem_write(cpu->edi, get(reg_a,z),z); \
    BUMP_DI(z)
#define str_lods(z) \
    set(reg_a, mem_read(cpu->esi, z),z); \
    BUMP_SI(z)
#define str_scas(z) \
    CMP(reg_a, mem_di,z); \
    BUMP_DI(z)
#define str_cmps(z) \
    CMP(mem_di, mem_si,z); \
    BUMP_SI_DI(z)

#define STR(op, z) str_##op(z)

#define REP(op, z) \
    while (cpu->ecx != 0) { \
        STR(op, z); \
        cpu->ecx--; \
    }

#define REPNZ(op, z) \
    while (cpu->ecx != 0) { \
        STR(op, z); \
        cpu->ecx--; \
        if (ZF) break; \
    }

#define REPZ(op, z) \
    while (cpu->ecx != 0) { \
        STR(op, z); \
        cpu->ecx--; \
        if (!ZF) break; \
    }

#define CMPXCHG(src, dst,z) \
    CMP(reg_a, dst,z); \
    if (E) { \
        MOV(src, dst,z); \
    } else \
        MOV(dst, reg_a,z)

#define XADD(src, dst,z) \
    XCHG(src, dst,z); \
    ADD(src, dst,z)

#define BSWAP(dst) \
    set(dst, __builtin_bswap32(get(dst,32)),32)

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

#define JMP(loc) cpu->eip = get(loc,); FIX_EIP;
#define JMP_REL(offset) cpu->eip += get(offset,); FIX_EIP;
#define J_REL(cond, offset) \
    if (cond) { \
        cpu->eip += get(offset,); FIX_EIP; \
    }
#define JN_REL(cond, offset) \
    if (!cond) { \
        cpu->eip += get(offset,); FIX_EIP; \
    }
#define JCXZ_REL(offset) J_REL(get(reg_c,oz) == 0, offset)

#define RET_NEAR(imm) POP(eip,32); FIX_EIP; cpu->esp += get(imm,16)

#define SET(cond, val) \
    set(val, (cond ? 1 : 0),8)
#define SETN(cond, val) \
    set(val, (cond ? 0 : 1),8)

#define CMOV(cond, dst, src,z) if (cond) MOV(dst, src,z)
#define CMOVN(cond, dst, src,z) if (!cond) MOV(dst, src,z)

#define POPF() \
    POP(eflags,32); \
    expand_flags(cpu)

#define PUSHF() \
    collapse_flags(cpu); \
    PUSH(eflags,oz)

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

// atomic
#define ATOMIC_ADD ADD
#define ATOMIC_OR OR
#define ATOMIC_ADC ADC
#define ATOMIC_SBB SBB
#define ATOMIC_AND AND
#define ATOMIC_SUB SUB
#define ATOMIC_XOR XOR
#define ATOMIC_INC INC
#define ATOMIC_DEC DEC
#define ATOMIC_CMPXCHG CMPXCHG
#define ATOMIC_XADD XADD
#define ATOMIC_BTS BTS
#define ATOMIC_BTR BTR
#define ATOMIC_BTC BTC

#include "emu/interp/fpu.h"

// fake sse
#define VLOAD(src, dst,z) UNDEFINED
#define VSTORE(src, dst,z) UNDEFINED

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
