#ifndef MODRM_H
#define MODRM_H

#include "misc.h"
#include "emu/cpu.h"

struct regptr {
    // offsets into the cpu_state structure
    reg_id_t reg8_id;
    reg_id_t reg16_id;
    reg_id_t reg32_id;
    reg_id_t reg128_id;
};
#if DEBUG_LOG
static const char *regptr_name(struct regptr regptr) {
    static char buf[15];
    sprintf(buf, "%s/%s/%s",
            reg8_name(regptr.reg8_id),
            reg16_name(regptr.reg16_id),
            reg32_name(regptr.reg32_id));
    return buf;
}
#endif

struct modrm_info {
    // MOD/RM BITS
    bool sib;
    enum {
        mod_disp0,
        mod_disp8,
        mod_disp32,
        mod_reg,
    } type;
    struct regptr modrm_regid;

    // REG BITS
    // offsets of the register into the cpu_state structure
    struct regptr reg;
    // for when it's not a register
    uint8_t opcode;
};

#ifdef DISABLE_MODRM_TABLE
#define modrm_get_info modrm_compute_info
struct modrm_info modrm_compute_info(byte_t byte);
#else
struct modrm_info modrm_table[0x100];
static inline struct modrm_info modrm_get_info(byte_t byte) {
    struct modrm_info info = modrm_table[byte];
    TRACE("reg %s opcode %d ", regptr_name(info.reg), info.opcode);
    switch (info.type) {
        case mod_reg:
            TRACE("mod_reg ");
            break;
        case mod_disp0:
            TRACE("mod_disp0 ");
            break;
        case mod_disp8:
            TRACE("mod_disp8 ");
            break;
        case mod_disp32:
            TRACE("mod_disp32 ");
            break;
    }
    if (info.sib) {
        TRACE("with sib ");
    } else {
        TRACE("%s ", regptr_name(info.modrm_regid));
    }
    return info;
}
#endif

#define MOD(byte) ((byte & 0b11000000) >> 6)
#define REG(byte) ((byte & 0b00111000) >> 3)
#define RM(byte)  ((byte & 0b00000111) >> 0)

void modrm_decode32(struct cpu_state *cpu, addr_t *addr_out, struct modrm_info *info_out);

#endif
