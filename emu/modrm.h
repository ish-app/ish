#include "misc.h"
#include "emu/cpu.h"

struct regptr {
    // offsets into the cpu_state structure
    reg_id_t reg8_id;
    reg_id_t reg16_id;
    reg_id_t reg32_id;
    reg_id_t reg64_id;
    reg_id_t reg64_high_id;
};
static const char *regptr_name(struct regptr regptr) {
    static char buf[15];
    sprintf(buf, "%s/%s/%s",
            reg8_name(regptr.reg8_id),
            reg16_name(regptr.reg16_id),
            reg32_name(regptr.reg32_id));
    return buf;
}

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
            TRACE("mod_disp32");
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

// Decodes ModR/M and SIB byte pointed to by cpu->eip, increments cpu->eip past
// them, and returns everything in out parameters.
// TODO currently only does 32-bit
static inline void modrm_decode32(struct cpu_state *cpu, addr_t *addr_out, struct modrm_info *info_out) {
    byte_t modrm = MEM_GET(cpu, cpu->eip, 8);
    struct modrm_info info = modrm_get_info(modrm);
    cpu->eip++;
    *info_out = info;
    if (info.type == mod_reg) return;
    *addr_out = 0;

    if (!info.sib) {
        if (info.modrm_regid.reg32_id != 0) {
            *addr_out += REG_VAL(cpu, info.modrm_regid.reg32_id, 32);
        }
    } else {
        // sib is simple enough to not use a table for
        byte_t sib = MEM_GET(cpu, cpu->eip, 8);
        TRACE("sib %x ", sib);
        cpu->eip++;
        dword_t reg = 0;
        switch (REG(sib)) {
            case 0b000: reg += cpu->eax; break;
            case 0b001: reg += cpu->ecx; break;
            case 0b010: reg += cpu->edx; break;
            case 0b011: reg += cpu->ebx; break;
            case 0b101: reg += cpu->ebp; break;
            case 0b110: reg += cpu->esi; break;
            case 0b111: reg += cpu->edi; break;
        }
        switch (MOD(sib)) {
            case 0b01: reg *= 2; break;
            case 0b10: reg *= 4; break;
            case 0b11: reg *= 8; break;
        }
        switch (RM(sib)) {
            case 0b000: reg += cpu->eax; break;
            case 0b001: reg += cpu->ecx; break;
            case 0b010: reg += cpu->edx; break;
            case 0b011: reg += cpu->ebx; break;
            case 0b100: reg += cpu->esp; break;
            case 0b101:
                // i know this is weird but this is what intel says
                if (info.type == mod_disp0) {
                    info.type = mod_disp32;
                } else {
                    reg += cpu->ebp;
                }
                break;
            case 0b110: reg += cpu->esi; break;
            case 0b111: reg += cpu->edi; break;
        }
        *addr_out += reg;
    }

    int disp;
    switch (info.type) {
        case mod_disp8: {
            disp = (int8_t) MEM_GET(cpu, cpu->eip, 8);
            TRACE("disp %s0x%x ", (disp < 0 ? "-" : ""), (disp < 0 ? -disp : disp));
            *addr_out += disp;
            cpu->eip++;
            break;
        }
        case mod_disp32: {
            disp = (int32_t) MEM_GET(cpu, cpu->eip, 32);
            TRACE("disp %s0x%x ", (disp < 0 ? "-" : ""), (disp < 0 ? -disp : disp));
            *addr_out += disp;
            cpu->eip += 4;
            break;
        }

        // shut up compiler I don't want to handle other cases
        default:;
    }
}
