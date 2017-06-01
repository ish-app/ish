#include "emu/cpuid.h"

#define MOV(src, dst) \
    dst = src

#define PUSH(thing) \
    sp -= OP_SIZE/8; \
    MEM_W(sp) = thing
#define POP(thing) \
    thing = MEM(sp); \
    sp += OP_SIZE/8

#define INT(code) \
    return code

#define SETRES(result) \
    cpu->res = (result); cpu->zf_res = cpu->sf_res = cpu->pf_res = 1

#define TEST(src, dst) \
    cpu->res = (dst) & (src); \
    cpu->cf = cpu->of = 0

#define ADD(src, dst) \
    cpu->cf = __builtin_add_overflow((uint32_t) (dst), (uint32_t) (src), (uint32_t *) &cpu->res); \
    cpu->of = __builtin_add_overflow((int32_t) (dst), (int32_t) (src), (int32_t *) &cpu->res); \
    (dst) = cpu->res

#define OR(src, dst) \
    (dst) |= (src); \
    cpu->cf = cpu->of = 0; \
    SETRES(dst)

#define AND(src, dst) \
    (dst) &= (src); \
    cpu->cf = cpu->of = 0; \
    SETRES(dst)

#define SUB(src, dst) \
    cpu->cf = __builtin_sub_overflow((uint32_t) (dst), (uint32_t) (src), (uint32_t *) &cpu->res); \
    cpu->of = __builtin_sub_overflow((int32_t) (dst), (int32_t) (src), (int32_t *) &cpu->res); \
    (dst) = cpu->res

// TODO flags
#define XOR(src, dst) dst ^= src;

#define CMP(src, dst) \
    cpu->cf = __builtin_sub_overflow((uint32_t) (dst), (uint32_t) (src), (uint32_t *) &cpu->res); \
    cpu->of = __builtin_sub_overflow((int32_t) (dst), (int32_t) (src), (int32_t *) &cpu->res)

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

#define IMUL(reg, val) \
    reg *= val
    // TODO flags

#define DIV(reg, val, rem) \
    if (val == 0) return INT_DIV; \
    rem = reg % val; reg = reg / val;

#define CALL(loc) PUSH(cpu->eip); JMP(loc)
#define CALL_REL(offset) PUSH(cpu->eip); JMP_REL(offset)

#define GRP1(src, dst) \
    switch (modrm.opcode) { \
        case 0b000: TRACE("add"); \
                    ADD(src, dst); break; \
        case 0b001: TRACE("or"); \
                    OR(src, dst); break; \
        case 0b100: TRACE("and"); \
                    AND(src, dst); break; \
        case 0b101: TRACE("sub"); \
                    SUB(src, dst); break; \
        case 0b111: TRACE("cmp"); \
                    CMP(src, dst); break; \
        default: \
            TRACE("undefined"); \
            return INT_UNDEFINED; \
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

#define GRP3(val) \
    switch (modrm.opcode) { \
        case 0: \
        case 1: TRACE("test imm"); \
                READIMM; TEST(imm, val); break; \
        case 2: TRACE("not"); return INT_UNDEFINED; \
        case 3: TRACE("neg"); return INT_UNDEFINED; \
        case 4: TRACE("mul"); return INT_UNDEFINED; \
        case 5: TRACE("imul"); return INT_UNDEFINED; \
        case 6: TRACE("div"); \
                DIV(ax, modrm_val, dx); break; \
        case 7: TRACE("idiv"); return INT_UNDEFINED; \
        default: TRACE("undefined"); return INT_UNDEFINED; \
    }

#define GRP5(val) \
    switch (modrm.opcode) { \
        case 0: TRACE("inc"); \
                INC(val); break; \
        case 1: TRACE("dec"); \
                DEC(val); break; \
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

#define BUMP_SI_DI(size) \
    if (!cpu->df) { \
        di += size; si += size; \
    } else { \
        di -= size; si -= size; \
    }

#define MOVS \
    MEM_W(di) = MEM(si); \
    BUMP_SI_DI(OP_SIZE/8);

#define MOVSB \
    MEM8_W(di) = MEM8(si); \
    BUMP_SI_DI(1);

#define REP(OP) \
    while (cx != 0) { \
        OP; \
        cx--; \
    }

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
#define PF (cpu->pf_res ? __builtin_parity(cpu->res) : cpu->pf)

#define FIX_EIP \
    if (OP_SIZE == 16) { \
        cpu->eip &= 0xffff; \
    }

#define JMP(loc) cpu->eip = loc; FIX_EIP;
#define JMP_REL(offset) cpu->eip += offset; FIX_EIP;
#define J_REL(cond, offset) \
    if (cond) { \
        cpu->eip += offset; FIX_EIP; \
    }

#define SET(cond, val) \
    val = (cond ? 1 : 0);

#define RET_NEAR() POP(cpu->eip); FIX_EIP;

static inline dword_t do_shift(dword_t val, dword_t amount, int op) {
    switch (op) {
        case 0:
            TRACE("rol");
            break;
        case 1:
            TRACE("ror");
            break;
        case 2:
            TRACE("rcl");
            break;
        case 3:
            TRACE("rcr");
            break;
        case 4:
        case 6:
            TRACE("shl");
            return val << amount;
        case 5:
            TRACE("shr");
            return val >> amount;
        case 7:
            TRACE("sar");
            return ((int32_t) val) >> amount;
    }
    return 0;
}
