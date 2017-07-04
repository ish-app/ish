#include "emu/cpu.h"
#include "emu/cpuid.h"

#define DECLARE_LOCALS \
    dword_t saved_ip = cpu->eip; \
    byte_t insn; \
    struct modrm_info modrm; \
    dword_t addr = 0; \
    \
    uint64_t imm; \
    union xmm_reg xmm_src; \
    union xmm_reg xmm_dst

#define READMODRM modrm_decode32(cpu, &addr, &modrm)
#define READIMM_(name,size) \
    name = mem_read(cpu->eip, size); \
    cpu->eip += size/8; \
    TRACE("imm %lx ", (uint64_t) name)
#define READIMM READIMM_(imm, OP_SIZE)
#define READIMM8 READIMM_(imm, 8)
#define READIMM16 READIMM_(imm, 16)
#define READINSN \
    insn = mem_read(cpu->eip, 8); \
    cpu->eip++; \
    TRACE("%02x ", insn);

// this is a completely insane way to turn empty into OP_SIZE and any other size into itself
#define sz(x) sz_##x
#define sz_ OP_SIZE
#define sz_8 8
#define sz_16 16
#define sz_32 32
#define sz_64 64
#define sz_128 128

// types for different sizes
#define ty(x) ty_##x
#define ty_8 uint8_t
#define ty_16 uint16_t
#define ty_32 uint32_t
#define ty_64 uint64_t
#define ty_128 union xmm_reg

#define mem_ptr(addr, type) ({ \
    void *ptr = mem_##type##_ptr(&cpu->mem, addr); \
    if (ptr == NULL) { \
        cpu->eip = saved_ip; \
        cpu->segfault_addr = addr; \
        return INT_GPF; \
    } \
    ptr; \
})

#define mem_read_type(addr, type) (*(type *) mem_ptr(addr, read))
#define mem_read(addr, size) mem_read_type(addr, ty(size))
#define mem_write_type(addr, val, type) *(type *) mem_ptr(addr, write) = val
#define mem_write(addr, val, size) mem_write_type(addr, val, ty(size))

#define get(what, size) get_##what(sz(size))
#define set(what, to, size) set_##what(to, sz(size))
#define is_memory(what) is_memory_##what

#define REGISTER(regptr, size) (*(ty(size) *) (((char *) cpu) + regptr.reg##size##_id))

#define get_modrm_reg(size) REGISTER(modrm.reg, size)
#define set_modrm_reg(to, size) REGISTER(modrm.reg, size) = to
#define is_memory_modrm_reg 0

#define is_memory_modrm_val (modrm.type != mod_reg)
#define get_modrm_val(size) \
    (modrm.type == mod_reg ? \
     REGISTER(modrm.modrm_regid, size) : \
     mem_read(addr, size))

#define set_modrm_val(to, size) \
    if (modrm.type == mod_reg) { \
        REGISTER(modrm.modrm_regid, size) = to; \
    } else { \
        mem_write(addr, to, size); \
    }(void)0

#define get_imm(size) ((uint(size)) imm)
#define get_imm8(size) ((int8_t) (uint8_t) imm)

#define get_mem_addr(size) mem_read(addr, size)
#define set_mem_addr(to, size) mem_write(addr, to, size)

// DEFINE ALL THE MACROS
#define get_oax(size) cpu->oax
#define get_obx(size) cpu->obx
#define get_ocx(size) cpu->ocx
#define get_odx(size) cpu->odx
#define get_osi(size) cpu->osi
#define get_odi(size) cpu->odi
#define get_obp(size) cpu->obp
#define get_osp(size) cpu->osp
#define get_eax(size) cpu->eax
#define get_ebx(size) cpu->ebx
#define get_ecx(size) cpu->ecx
#define get_edx(size) cpu->edx
#define get_esi(size) cpu->esi
#define get_edi(size) cpu->edi
#define get_ebp(size) cpu->ebp
#define get_esp(size) cpu->esp
#define get_eip(size) cpu->eip
#define get_eflags(size) cpu->eflags
#define get_ax(size) cpu->ax
#define get_bx(size) cpu->bx
#define get_cx(size) cpu->cx
#define get_dx(size) cpu->dx
#define get_si(size) cpu->si
#define get_di(size) cpu->di
#define get_bp(size) cpu->bp
#define get_sp(size) cpu->sp
#define get_al(size) cpu->al
#define get_bl(size) cpu->bl
#define get_cl(size) cpu->cl
#define get_dl(size) cpu->dl
#define get_ah(size) cpu->ah
#define get_bh(size) cpu->bh
#define get_ch(size) cpu->ch
#define get_dh(size) cpu->dh
#define set_oax(to, size) cpu->oax = to
#define set_obx(to, size) cpu->obx = to
#define set_ocx(to, size) cpu->ocx = to
#define set_odx(to, size) cpu->odx = to
#define set_osi(to, size) cpu->osi = to
#define set_odi(to, size) cpu->odi = to
#define set_obp(to, size) cpu->obp = to
#define set_osp(to, size) cpu->osp = to
#define set_eax(to, size) cpu->eax = to
#define set_ebx(to, size) cpu->ebx = to
#define set_ecx(to, size) cpu->ecx = to
#define set_edx(to, size) cpu->edx = to
#define set_esi(to, size) cpu->esi = to
#define set_edi(to, size) cpu->edi = to
#define set_ebp(to, size) cpu->ebp = to
#define set_esp(to, size) cpu->esp = to
#define set_eip(to, size) cpu->eip = to
#define set_eflags(to, size) cpu->eflags = to
#define set_ax(to, size) cpu->ax = to
#define set_bx(to, size) cpu->bx = to
#define set_cx(to, size) cpu->cx = to
#define set_dx(to, size) cpu->dx = to
#define set_si(to, size) cpu->si = to
#define set_di(to, size) cpu->di = to
#define set_bp(to, size) cpu->bp = to
#define set_sp(to, size) cpu->sp = to
#define set_al(to, size) cpu->al = to
#define set_bl(to, size) cpu->bl = to
#define set_cl(to, size) cpu->cl = to
#define set_dl(to, size) cpu->dl = to
#define set_ah(to, size) cpu->ah = to
#define set_bh(to, size) cpu->bh = to
#define set_ch(to, size) cpu->ch = to
#define set_dh(to, size) cpu->dh = to

#define get_0(size) 0
#define get_1(size) 1

// only used by lea
#define get_addr(size) addr

// INSTRUCTION MACROS
// if an instruction accesses memory, it should do that before it modifies
// registers, so segfault recovery only needs to save IP.

// takes any unsigned integer and casts it to signed of the same size

#define unsigned_overflow(what, a, b, res, z) ({ \
    int ov = __builtin_##what##_overflow((uint(sz(z))) (a), (uint(sz(z))) (b), (uint(sz(z)) *) &res); \
    res = (sint(sz(z))) res; ov; \
})
#define signed_overflow(what, a, b, res, z) ({ \
    int ov = __builtin_##what##_overflow((sint(sz(z))) (a), (sint(sz(z))) (b), (sint(sz(z)) *) &res); \
    res = (sint(sz(z))) res; ov; \
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

#define PUSH(thing) \
    mem_write(cpu->osp - OP_SIZE/8, get(thing, OP_SIZE), OP_SIZE); \
    cpu->osp -= OP_SIZE/8
#define POP(thing) \
    set(thing, mem_read(cpu->osp, OP_SIZE),); \
    cpu->osp += OP_SIZE/8

#define INT(code) \
    return get(code,8)

// math

#define SETRESFLAGS cpu->zf_res = cpu->sf_res = cpu->pf_res = 1
#define SETRES_RAW(result,z)
#define SETRES(result,z) \
    cpu->res = (int32_t) (sint(sz(z))) (result); SETRESFLAGS
    // sign extend result so SF is correct
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
        || (get(src,z) + cpu->cf == -1); \
    cpu->cf = unsigned_overflow(add, get(dst,z), get(src,z) + cpu->cf, cpu->res,z) \
        || (cpu->cf && get(src,z) == (uint(sz(z))) -1); \
    set(dst, cpu->res,z); SETRESFLAGS

#define SBB(src, dst,z) \
    SETAF(src, dst,z); \
    cpu->of = signed_overflow(sub, get(dst,z), get(src,z) + cpu->cf, cpu->res,z) \
        || (get(src,z) + cpu->cf == -1); \
    cpu->cf = unsigned_overflow(sub, get(dst,z), get(src,z) + cpu->cf, cpu->res,z) \
        || (cpu->cf && get(src,z) == (uint(sz(z))) -1); \
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
    uint64_t tmp = cpu->oax * (uint64_t) get(val,z); \
    cpu->oax = tmp; cpu->odx = tmp >> sz(z); \
    cpu->cf = cpu->of = (tmp != (uint32_t) tmp); ZEROAF; \
    cpu->zf = cpu->sf = cpu->pf = cpu->zf_res = cpu->sf_res = cpu->pf_res = 0; \
} while (0)
#define IMUL1(val,z) do { \
    int64_t tmp = (int64_t) (sint(sz(z))) cpu->oax * (sint(sz(z))) get(val,z); \
    cpu->oax = tmp; cpu->odx = tmp >> sz(z); \
    cpu->zf = cpu->sf = cpu->pf = cpu->zf_res = cpu->sf_res = cpu->pf_res = 0; \
} while (0)
#define MUL2(val, reg) \
    cpu->cf = cpu->of = unsigned_overflow(mul, reg, val, cpu->res,z); \
    set(reg, cpu->res,z); SETRESFLAGS
#define IMUL2(val, reg,z) \
    cpu->cf = cpu->of = signed_overflow(mul, get(reg,z), get(val,z), cpu->res,z); \
    set(reg, cpu->res,z); SETRESFLAGS
#define MUL3(imm, src, dst) \
    cpu->cf = cpu->of = unsigned_overflow(mul, get(src,z), get(imm,z), cpu->res,z); \
    set(dst, cpu->res,z); \
    cpu->pf_res = 1; cpu->zf = cpu->sf = cpu->zf_res = cpu->sf_res = 0
#define IMUL3(imm, src, dst,z) \
    cpu->cf = cpu->of = signed_overflow(mul, get(src,z), get(imm,z), cpu->res,z); \
    set(dst, cpu->res,z); \
    cpu->pf_res = 1; cpu->zf = cpu->sf = cpu->zf_res = cpu->sf_res = 0
#define _MUL_MODRM(val) \
    if (modrm.reg.reg32_id == modrm.modrm_regid.reg32_id) \
        modrm_reg *= val; \
    else \
        modrm_reg = val * modrm_val

#define DIV(reg, val, rem,z) \
    if (get(val,z) == 0) return INT_DIV; \
    set(rem, get(reg,z) % get(val,z),z); set(reg, get(reg,z) / get(val,z),z)

#define IDIV(reg, val, rem,z) \
    if (get(val,z) == 0) return INT_DIV; \
    set(rem, (int32_t) get(reg,z) % get(val,z),z); set(reg, (int32_t) get(reg,z) / get(val,z),z)

// TODO this is probably wrong in some subtle way
#define CDQ \
    cpu->odx = cpu->oax & (1 << (OP_SIZE - 1)) ? (uint(OP_SIZE)) -1 : 0; break;

#define CALL(loc) PUSH(eip); JMP(loc)
#define CALL_REL(offset) PUSH(eip); JMP_REL(offset)

#define ROL(count, val,z) \
    if (get(count,z) % sz(z) != 0) { \
        int cnt = get(count,z) % sz(z); \
        /* the compiler miraculously turns this into a rol instruction with optimizations on */\
        set(val, get(val,z) << cnt | get(val,z) >> (sz(z) - cnt),z); \
        cpu->cf = get(val,z) & 1; \
    }
#define ROR(count, val,z) \
    if (get(count,z) % sz(z) != 0) { \
        int cnt = get(count,z) % sz(z); \
        set(val, get(val,z) >> cnt | get(val,z) << (sz(z) - cnt),z); \
        cpu->cf = get(val,z) >> (OP_SIZE - 1); \
    }
#define SHL(count, val,z) \
    if (get(count,z) % sz(z) != 0) { \
        int cnt = get(count,z) % sz(z); \
        cpu->cf = (get(val,z) << (cnt - 1)) >> (sz(z) - 1); \
        cpu->of = cpu->cf ^ (get(val,z) >> (sz(z) - 1)); \
        set(val, get(val,z) << cnt,z); SETRES(get(val,z),z); ZEROAF; \
    }
#define SHR(count, val,z) \
    if (get(count,z) % sz(z) != 0) { \
        int cnt = get(count,z) % sz(z); \
        cpu->cf = (get(val,z) >> (cnt - 1)) & 1; \
        cpu->of = get(val,z) >> (sz(z) - 1); \
        set(val, get(val,z) >> cnt,z); SETRES(get(val,z),z); ZEROAF; \
    }
#define SAR(count, val,z) \
    if (get(count,z) % sz(z) != 0) { \
        int cnt = get(count,z) % sz(z); \
        cpu->cf = (get(val,z) >> (cnt - 1)) & 1; cpu->of = 0; \
        set(val, ((int32_t) get(val,z)) >> cnt,z); SETRES(get(val,z),z); ZEROAF; \
    }

#define SHRD(count, extra, dst,z) \
    if (get(count,z) != 0) { \
        cpu->res = get(dst,z) >> get(count,z) | get(extra,z) << (sz(z) - get(count,z)); \
        set(dst, cpu->res,z); \
        SETRESFLAGS; \
    }

#define SHLD(count, extra, dst,z) \
    if (get(count,z) != 0) { \
        cpu->res = get(dst,z) << get(count,z) | get(extra,z) >> (sz(z) - get(count,z)); \
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
                READIMM8; TEST(imm8, val); break; \
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

#define msk(bit,z) (1 << (get(bit,z) % sz(z)))

#define BT(bit, val,z) \
    cpu->cf = (get(val,z) & msk(bit,z)) ? 1 : 0;

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
    set(dst, __builtin_ctz(get(src,z)),z); \
    cpu->zf = get(src,z) == 0; \
    cpu->zf_res = 0

// string instructions

#define BUMP_SI(size) \
    if (!cpu->df) \
        cpu->esi += size; \
    else \
        cpu->esi -= size
#define BUMP_DI(size) \
    if (!cpu->df) \
        cpu->edi += size; \
    else \
        cpu->edi -= size
#define BUMP_SI_DI(size) \
    BUMP_SI(size); BUMP_DI(size)

#define MOVS(z) \
    mem_write(cpu->edi, mem_read(cpu->esi, sz(z)), sz(z)); \
    BUMP_SI_DI(sz(z)/8)

#define STOS(z) \
    mem_write(cpu->edi, cpu->oax, sz(z)); \
    BUMP_DI(sz(z)/8)

#define REP(OP) \
    while (cpu->ocx != 0) { \
        OP; \
        cpu->ocx--; \
    }

#define CMPXCHG(src, dst,z) \
    CMP(oax, dst,z); \
    if (E) { \
        MOV(src, dst,z); \
    } else \
        MOV(dst, oax,z)

#define XADD(src, dst,z) \
    XCHG(src, dst,z); \
    ADD(src, dst,z)

#define BSWAP(dst) \
    set(dst, __builtin_bswap32(get(dst,)),)

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
#define JCXZ_REL(offset) J_REL(cpu->ocx == 0, offset)

#define RET_NEAR() POP(eip); FIX_EIP
#define RET_NEAR_IMM(imm) RET_NEAR(); cpu->osp += get(imm,16)

#define SET(cond, val) \
    set(val, (cond ? 1 : 0),8)

#define CMOV(cond, dst, src,z) \
    if (cond) MOV(dst, src,z)

#define POPF() \
    POP(eflags); \
    cpu->zf_res = cpu->sf_res = cpu->pf_res = cpu->af_ops = 0

#define PUSHF() \
    collapse_flags(cpu); \
    PUSH(eflags)

#define STD cpu->df = 1
#define CLD cpu->df = 0

#include "emu/interp/sse.h"

// ok now include the decoding function
#define decoder_name cpu_step

#define OP_SIZE 32
#define oax eax
#define obx ebx
#define ocx ecx
#define odx edx
#define osi esi
#define odi edi
#define obp ebp
#define osp esp
#include "decode.h"
#undef OP_SIZE

#define OP_SIZE 16
#undef oax
#define oax ax
#undef obx
#define obx bx
#undef ocx
#define ocx cx
#undef odx
#define odx dx
#undef osi
#define osi si
#undef odi
#define odi di
#undef obp
#define obp bp
#undef osp
#define osp sp
#include "decode.h"
#undef OP_SIZE

flatten void cpu_run(struct cpu_state *cpu) {
    while (true) {
        int interrupt = cpu_step32(cpu);
        if (interrupt != INT_NONE) {
            handle_interrupt(cpu, interrupt);
        }
    }
}

