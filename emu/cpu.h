#ifndef EMU_H
#define EMU_H

#include <stddef.h>
#include <softfloat.h>
#include "misc.h"
#include "emu/memory.h"

struct cpu_state;
void cpu_run(struct cpu_state *cpu);
int cpu_step32(struct cpu_state *cpu);
int cpu_step16(struct cpu_state *cpu);

union xmm_reg {
    qword_t qw[2];
    dword_t dw[4];
    // TODO more forms
};

struct cpu_state {
    struct mem *mem;

    // assumes little endian (as does literally everything)
#define _REG(n) \
    union { \
        dword_t e##n; \
        word_t n; \
    };
#define _REGX(n) \
    union { \
        dword_t e##n##x; \
        word_t n##x; \
        struct { \
            byte_t n##l; \
            byte_t n##h; \
        }; \
    };

    _REGX(a);
    _REGX(b);
    _REGX(c);
    _REGX(d);
    _REG(si);
    _REG(di);
    _REG(bp);
    _REG(sp);
#undef REGX
#undef REG

    union xmm_reg xmm[8];

    dword_t eip;

    // flags
    union {
        dword_t eflags;
        struct {
            bits cf:1;
            bits pad1_1:1;
            bits pf:1;
            bits pad2_0:1;
            bits af:1;
            bits pad3_0:1;
            bits zf:1;
            bits sf:1;
            bits tf:1;
            bits if_:1;
            bits df:1;
            bits of:1;
            bits iopl:2;
        };
    };
    // whether the true flag values are in the above struct, or computed from
    // the stored result and operands
    dword_t res, op1, op2;
    bits pf_res:1;
    bits zf_res:1;
    bits sf_res:1;
    bits cf_ops:1;
    bits of_ops:1;
    bits af_ops:1;

    // fpu
    extFloat80_t fp[8];
    union {
        word_t fsw;
        struct {
            bits ie:1; // invalid operation
            bits de:1; // denormalized operand
            bits ze:1; // divide by zero
            bits oe:1; // overflow
            bits ue:1; // underflow
            bits pe:1; // precision
            bits stf:1; // stack fault
            bits es:1; // exception status
            bits c0:1;
            bits c1:1;
            bits c2:1;
            unsigned top:3;
            bits c3:1;
            bits b:1; // fpu busy (?)
        };
    };
    union {
        word_t fcw;
        struct {
            bits im:1;
            bits dm:1;
            bits zm:1;
            bits om:1;
            bits um:1;
            bits pm:1;
            bits pad4:2;
            bits pc:2;
            bits rc:2;
            bits y:1;
        };
    };

    // See comment in sys/tls.c
    addr_t tls_ptr;

    // for the page fault handler
    addr_t segfault_addr;

    dword_t trapno;
};

// flags
#define ZF (cpu->zf_res ? cpu->res == 0 : cpu->zf)
#define SF (cpu->sf_res ? (int32_t) cpu->res < 0 : cpu->sf)
#define CF (cpu->cf)
#define OF (cpu->of)
#define PF (cpu->pf_res ? !__builtin_parity(cpu->res & 0xff) : cpu->pf)
#define AF (cpu->af_ops ? ((cpu->op1 ^ cpu->op2 ^ cpu->res) >> 4) & 1 : cpu->af)

static inline void collapse_flags(struct cpu_state *cpu) {
    cpu->zf = ZF;
    cpu->sf = SF;
    cpu->pf = PF;
    cpu->zf_res = cpu->sf_res = cpu->pf_res = 0;
    cpu->af = AF;
    cpu->af_ops = 0;
    cpu->pad1_1 = 1;
    cpu->pad2_0 = cpu->pad3_0 = 0;
    cpu->if_ = 1;
}

typedef uint8_t reg_id_t;
#define REG_ID(reg) offsetof(struct cpu_state, reg)
#define REG_VAL(cpu, reg_id, size) (*((uint(size) *) (((char *) (cpu)) + reg_id)))
inline const char *reg8_name(uint8_t reg_id) {
    switch (reg_id) {
        case REG_ID(al): return "al";
        case REG_ID(bl): return "bl";
        case REG_ID(cl): return "cl";
        case REG_ID(dl): return "dl";
        case REG_ID(ah): return "ah";
        case REG_ID(bh): return "bh";
        case REG_ID(ch): return "ch";
        case REG_ID(dh): return "dh";
    }
    return "??";
}
inline const char *reg16_name(uint8_t reg_id) {
    switch (reg_id) {
        case REG_ID(ax): return "ax";
        case REG_ID(bx): return "bx";
        case REG_ID(cx): return "cx";
        case REG_ID(dx): return "dx";
        case REG_ID(si): return "si";
        case REG_ID(di): return "di";
        case REG_ID(bp): return "bp";
        case REG_ID(sp): return "sp";
    }
    return "??";
}
inline const char *reg32_name(uint8_t reg_id) {
    switch (reg_id) {
        case REG_ID(eax): return "eax";
        case REG_ID(ebx): return "ebx";
        case REG_ID(ecx): return "ecx";
        case REG_ID(edx): return "edx";
        case REG_ID(esi): return "esi";
        case REG_ID(edi): return "edi";
        case REG_ID(ebp): return "ebp";
        case REG_ID(esp): return "esp";
    }
    return "???";
}

#endif
