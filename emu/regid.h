#ifndef EMU_REGID_H
#define EMU_REGID_H
#include "emu/cpu.h"

typedef uint8_t reg_id_t;
#define REG_ID(reg) offsetof(struct cpu_state, reg)
#define REG_VAL(cpu, reg_id, size) (*((uint(size) *) (((char *) (cpu)) + reg_id)))
static inline const char *regid8_name(uint8_t reg_id) {
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
static inline const char *regid16_name(uint8_t reg_id) {
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
static inline const char *regid32_name(uint8_t reg_id) {
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

struct regptr {
    // offsets into the cpu_state structure
    reg_id_t reg8_id;
    reg_id_t reg16_id;
    reg_id_t reg32_id;
    reg_id_t reg128_id;
};
static __attribute__((unused)) const char *regptr_name(struct regptr regptr) {
    static char buf[15];
    sprintf(buf, "%s/%s/%s",
            regid8_name(regptr.reg8_id),
            regid16_name(regptr.reg16_id),
            regid32_name(regptr.reg32_id));
    return buf;
}

#define MAKE_REGPTR(r32, r16, r8, xmm) ((struct regptr) { \
        .reg32_id = REG_ID(r32), \
        .reg16_id = REG_ID(r16), \
        .reg8_id = REG_ID(r8), \
        .reg128_id = REG_ID(xmm), \
        })

static inline struct regptr regptr_from_reg(enum reg32 reg) {
    switch (reg) {
        case reg_eax: return MAKE_REGPTR(eax,ax,al,xmm[0]);
        case reg_ecx: return MAKE_REGPTR(ecx,cx,cl,xmm[1]);
        case reg_edx: return MAKE_REGPTR(edx,dx,dl,xmm[2]);
        case reg_ebx: return MAKE_REGPTR(ebx,bx,bl,xmm[3]);
        case reg_esp: return MAKE_REGPTR(esp,sp,ah,xmm[4]);
        case reg_ebp: return MAKE_REGPTR(ebp,bp,ch,xmm[5]);
        case reg_esi: return MAKE_REGPTR(esi,si,dh,xmm[6]);
        case reg_edi: return MAKE_REGPTR(edi,di,bh,xmm[7]);
        case reg_none: return (struct regptr) {};
        default: die("invalid register");
    }
}

#endif
