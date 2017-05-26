#include "emu/modrm.h"

#define MOD(byte) ((byte & 0b11000000) >> 6)
#define REG(byte) ((byte & 0b00111000) >> 3)
#define RM(byte)  ((byte & 0b00000111) >> 0)

#define MAKE_REGPTR(r32, r16, r8, xmm) ((struct regptr) { \
        .reg32_id = REG_ID(r32), \
        .reg16_id = REG_ID(r16), \
        .reg8_id = REG_ID(r8), \
        .reg64_id = REG_ID(xmm.qlow), \
        .reg64_high_id = REG_ID(xmm.qhigh) \
        })

static inline struct regptr decode_reg(byte_t reg) {
    switch (reg) {
        case 0b000: return MAKE_REGPTR(eax,ax,al,xmm[0]);
        case 0b001: return MAKE_REGPTR(ecx,cx,cl,xmm[1]);
        case 0b010: return MAKE_REGPTR(edx,dx,dl,xmm[2]);
        case 0b011: return MAKE_REGPTR(ebx,bx,bl,xmm[3]);
        case 0b100: return MAKE_REGPTR(esp,sp,ah,xmm[4]);
        case 0b101: return MAKE_REGPTR(ebp,bp,ch,xmm[5]);
        case 0b110: return MAKE_REGPTR(esi,si,dh,xmm[6]);
        case 0b111: return MAKE_REGPTR(edi,di,bh,xmm[7]);
    }
    fprintf(stderr, "fuck\n"); abort();
}

struct modrm_info modrm_compute_info(byte_t byte) {
    struct modrm_info info;
    info.opcode = REG(byte);
    info.sib = false;
    info.reg = decode_reg(REG(byte));
    info.modrm_regid = decode_reg(RM(byte));
    switch (MOD(byte)) {
        case 0b00:
            // [reg], disp32, [sib]
            info.type = mod_disp0;
            switch (RM(byte)) {
                case 0b100:
                    info.sib = true; break;
                case 0b101:
                    info.type = mod_disp32;
                    info.modrm_regid = (struct regptr) {0,0,0};
                    break;
            }
            break;
        case 0b01:
            // disp8[reg], disp8[sib]
            info.type = mod_disp8;
            if (RM(byte) == 0b100) {
                info.sib = true;
            }
            break;
        case 0b10:
            // disp32[reg], disp32[sib]
            info.type = mod_disp32;
            if (RM(byte) == 0b100) {
                info.sib = true;
            }
            break;
        case 0b11:
            // reg
            info.type = mod_reg;
            break;
    }
    return info;
}

#ifndef DISABLE_MODRM_TABLE

static void __attribute__((constructor)) modrm_table_build(void) {
    for (int byte = 0; byte <= UINT8_MAX; byte++) {
        modrm_table[byte] = modrm_compute_info(byte);
    }
}

extern inline struct modrm_info modrm_get_info(byte_t byte);

#endif

extern inline void modrm_decode32(struct cpu_state *cpu, addr_t *addr_out, struct modrm_info *info_out);
