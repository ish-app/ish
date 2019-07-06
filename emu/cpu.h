#ifndef EMU_H
#define EMU_H

#include <stddef.h>
#include "misc.h"
#include "emu/float80.h"
#include "emu/memory.h"

struct cpu_state;
struct tlb;
void cpu_run(struct cpu_state *cpu);
int cpu_step32(struct cpu_state *cpu, struct tlb *tlb);
int cpu_step16(struct cpu_state *cpu, struct tlb *tlb);

union xmm_reg {
    qword_t qw[2];
    dword_t dw[4];
    // TODO more forms
};

struct cpu_state {
    struct mem *mem;
    struct jit *jit;

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
            bits cf_bit:1;
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
            bits of_bit:1;
            bits iopl:2;
        };
        // for asm
#define PF_FLAG (1 << 2)
#define AF_FLAG (1 << 4)
#define ZF_FLAG (1 << 6)
#define SF_FLAG (1 << 7)
#define DF_FLAG (1 << 10)
    };
    // please pretend this doesn't exist
    dword_t df_offset;
    // for maximum efficiency these are stored in bytes
    byte_t cf;
    byte_t of;
    // whether the true flag values are in the above struct, or computed from
    // the stored result and operands
    dword_t res, op1, op2;
    union {
        struct {
            bits pf_res:1;
            bits zf_res:1;
            bits sf_res:1;
            bits af_ops:1;
        };
        // for asm
#define PF_RES (1 << 0)
#define ZF_RES (1 << 1)
#define SF_RES (1 << 2)
#define AF_OPS (1 << 3)
        byte_t flags_res;
    };

    // fpu
    float80 fp[8];
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

    // TLS bullshit
    word_t gs;
    addr_t tls_ptr;

    // for the page fault handler
    addr_t segfault_addr;
    uint8_t segfault_type;

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
    cpu->of_bit = cpu->of;
    cpu->cf_bit = cpu->cf;
    cpu->af = AF;
    cpu->af_ops = 0;
    cpu->pad1_1 = 1;
    cpu->pad2_0 = cpu->pad3_0 = 0;
    cpu->if_ = 1;
}

static inline void expand_flags(struct cpu_state *cpu) {
    cpu->of = cpu->of_bit;
    cpu->cf = cpu->cf_bit;
    cpu->zf_res = cpu->sf_res = cpu->pf_res = cpu->af_ops = 0;
}

enum reg32 {
    reg_eax = 0, reg_ecx, reg_edx, reg_ebx, reg_esp, reg_ebp, reg_esi, reg_edi, reg_count,
    reg_none = reg_count,
};

static inline const char *reg32_name(enum reg32 reg) {
    switch (reg) {
        case reg_eax: return "eax";
        case reg_ecx: return "ecx";
        case reg_edx: return "edx";
        case reg_ebx: return "ebx";
        case reg_esp: return "esp";
        case reg_ebp: return "ebp";
        case reg_esi: return "esi";
        case reg_edi: return "edi";
        case reg_none: return "?";
    }
}

#endif
