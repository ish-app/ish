#include "emu/cpuid.h"

// if an instruction accesses memory, it should do that before it modifies
// registers, so segfault recovery only needs to save IP.

#define MOV(src, dst) \
    (dst) = (src)

#define XCHG(src, dst) do { \
    dword_t tmp = (src); \
    (src) = (dst); \
    (dst) = tmp; \
} while (0)

#define PUSH(thing) \
    MEM_W(sp - OP_SIZE/8) = thing; \
    sp -= OP_SIZE/8
#define POP(thing) \
    thing = MEM(sp); \
    sp += OP_SIZE/8

#define INT(code) \
    return code

#define SETRESFLAGS cpu->zf_res = cpu->sf_res = cpu->pf_res = 1
#define SETRES(result) \
    cpu->res = (result); SETRESFLAGS

#define TEST(src, dst) \
    cpu->res = (dst) & (src); \
    cpu->cf = cpu->of = 0

#define ADD(src, dst) \
    cpu->cf = __builtin_add_overflow((uint32_t) (dst), (uint32_t) (src), (uint32_t *) &cpu->res); \
    cpu->of = __builtin_add_overflow((int32_t) (dst), (int32_t) (src), (int32_t *) &cpu->res); \
    (dst) = cpu->res; SETRESFLAGS

// had a nasty bug because ADD overwrites cpu->cf
#define ADC(src, dst) do { \
    int tmp = cpu->cf; \
    ADD(src + tmp, dst); \
    cpu->cf = tmp; \
} while (0)

#define SBB(src, dst) do { \
    int tmp = cpu->cf; \
    SUB(src + tmp, dst); \
    cpu->cf = tmp; \
} while (0)

#define OR(src, dst) \
    (dst) |= (src); \
    cpu->cf = cpu->of = 0; SETRES(dst)

#define AND(src, dst) \
    (dst) &= (src); \
    cpu->cf = cpu->of = 0; SETRES(dst)

#define SUB(src, dst) \
    cpu->cf = __builtin_sub_overflow((uint32_t) (dst), (uint32_t) (src), (uint32_t *) &cpu->res); \
    cpu->of = __builtin_sub_overflow((int32_t) (dst), (int32_t) (src), (int32_t *) &cpu->res); \
    (dst) = cpu->res; SETRESFLAGS

// TODO flags
#define XOR(src, dst) \
    (dst) ^= (src); \
    cpu->cf = cpu->of = 0; SETRES(dst)

#define CMP(src, dst) \
    cpu->cf = __builtin_sub_overflow((uint32_t) (dst), (uint32_t) (src), (uint32_t *) &cpu->res); \
    cpu->of = __builtin_sub_overflow((int32_t) (dst), (int32_t) (src), (int32_t *) &cpu->res); \
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

// TODO flags
#define MUL18(val) cpu->ax_ = cpu->al * val
#define MUL1(val) do { \
    uint64_t tmp = ax * (uint64_t) val; \
    ax = tmp; dx = tmp >> OP_SIZE; \
} while (0)
#define IMUL1(val) do { \
    int64_t tmp = ax * (int64_t) val; \
    ax = tmp; dx = tmp >> OP_SIZE; \
} while (0)
#define MUL2(val, reg) reg *= val
#define MUL3(imm, src, dst) dst = src * imm
#define _MUL_MODRM(val) \
    if (modrm.reg.reg32_id == modrm.modrm_regid.reg32_id) \
        modrm_reg *= val; \
    else \
        modrm_reg = val * modrm_val

#define DIV(reg, val, rem) \
    if (val == 0) return INT_DIV; \
    rem = reg % val; reg = reg / val;

#define CALL(loc) PUSH(cpu->eip); JMP(loc)
#define CALL_REL(offset) PUSH(cpu->eip); JMP_REL(offset)

#define GRP1(src, dst) \
    switch (modrm.opcode) { \
        case 0: TRACE("add"); \
                ADD(src, dst##_w); break; \
        case 1: TRACE("or"); \
                OR(src, dst##_w); break; \
        case 2: TRACE("adc"); \
                ADC(src, dst##_w); break; \
        case 4: TRACE("and"); \
                AND(src, dst##_w); break; \
        case 5: TRACE("sub"); \
                SUB(src, dst##_w); break; \
        case 6: TRACE("xor"); \
                XOR(src, dst##_w); break; \
        case 7: TRACE("cmp"); \
                CMP(src, dst); break; \
        default: TRACE("undefined"); \
                 return INT_UNDEFINED; \
    }

#define GRP2(count, val) \
    switch (modrm.opcode) { \
        case 0: TRACE("rol"); \
                /* the compiler miraculously turns this into a rol instruction at -O3 */\
                val = val << count | val >> (OP_SIZE - count); break; \
        case 1: TRACE("ror"); \
                val = val >> count | val << (OP_SIZE - count); break; \
        case 2: TRACE("rcl"); \
                return INT_UNDEFINED; \
        case 3: TRACE("rcr"); \
                return INT_UNDEFINED; \
        case 4: \
        case 6: TRACE("shl"); \
                val <<= count; break; \
        case 5: TRACE("shr"); \
                cpu->cf = val & 1; val >>= count; SETRES(val); break; \
        case 7: TRACE("sar"); \
                val = ((int32_t) val) >> count; break; \
    }

#define SHRD(count, extra, dst) \
    (dst) = (dst) >> (count) | (extra) << (OP_SIZE - count)

#define SHLD(count, extra, dst) \
    (dst) = (dst) << (count) | (extra) >> (OP_SIZE - count)


#define GRP3(val) \
    switch (modrm.opcode) { \
        case 0: \
        case 1: TRACE("test imm"); \
                READIMM; TEST(imm, val); break; \
        case 2: TRACE("not"); \
                val = ~val; break; TODO("flags"); \
        case 3: TRACE("neg"); \
                val = -val; break; TODO("flags"); \
        case 4: TRACE("mul"); \
                MUL1(modrm_val); break; \
        case 5: TRACE("imul"); return INT_UNDEFINED; \
        case 6: TRACE("div"); \
                DIV(ax, modrm_val, dx); break; \
        case 7: TRACE("idiv"); return INT_UNDEFINED; \
        default: TRACE("undefined"); return INT_UNDEFINED; \
    }

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
        default: TRACE("undefined"); return INT_UNDEFINED; \
    }

#define GRP5(val) \
    switch (modrm.opcode) { \
        case 0: TRACE("inc"); \
                INC(val##_w); break; \
        case 1: TRACE("dec"); \
                DEC(val##_w); break; \
        case 2: TRACE("call indirect near"); \
                CALL(modrm_val); break; \
        case 3: TRACE("call indirect far"); return INT_UNDEFINED; \
        case 4: TRACE("jmp indirect near"); \
                JMP(modrm_val); break; \
        case 5: TRACE("jmp indirect far"); return INT_UNDEFINED; \
        case 6: TRACE("push"); \
                PUSH(val); break; \
        case 7: TRACE("undefined"); return INT_UNDEFINED; \
    }

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

#define MOVS \
    MEM_W(cpu->edi) = MEM(cpu->esi); \
    BUMP_SI_DI(OP_SIZE/8)

#define MOVSB \
    MEM8_W(cpu->edi) = MEM8(cpu->esi); \
    BUMP_SI_DI(1)

#define STOS \
    MEM_W(cpu->edi) = ax; \
    BUMP_DI(OP_SIZE/8)

#define STOSB \
    MEM8_W(di) = cpu->al; \
    BUMP_DI(1)

#define REP(OP) \
    while (cx != 0) { \
        OP; \
        cx--; \
    }

#define CMPXCHG(src, dst) \
    CMP(ax, dst); \
    if (E) \
        MOV(src, dst##_w); \
    else \
        MOV(dst, ax)

// condition codes
#define E ZF
#define B CF
#define BE (CF | ZF)
#define L (SF ^ OF)
#define LE (L | ZF)
#define O OF
#define P PF
#define S SF

// flags
#define ZF (cpu->zf_res ? cpu->res == 0 : cpu->zf)
#define SF (cpu->sf_res ? (int32_t) cpu->res < 0 : cpu->sf)
#define CF (cpu->cf)
#define OF (cpu->of)
#define PF (cpu->pf_res ? !__builtin_parity(cpu->res) : cpu->pf)

#define FIX_EIP \
    if (OP_SIZE == 16) \
        cpu->eip &= 0xffff

#define JMP(loc) cpu->eip = loc; FIX_EIP;
#define JMP_REL(offset) cpu->eip += offset; FIX_EIP;
#define J_REL(cond, offset) \
    if (cond) { \
        cpu->eip += offset; FIX_EIP; \
    }

#define RET_NEAR() POP(cpu->eip); FIX_EIP
#define RET_NEAR_IMM(imm) RET_NEAR(); sp += imm

#define SET(cond, val) \
    val = (cond ? 1 : 0)

#define CMOV(cond, dst, src) \
    if (cond) MOV(dst, src)
