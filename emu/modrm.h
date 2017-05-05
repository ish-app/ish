#include "emu/cpu.h"
#include "emu/types.h"

struct regptr {
    // offsets into the cpu_state structure
    reg_id_t reg8_id;
    reg_id_t reg16_id;
    reg_id_t reg32_id;
};

struct modrm_info {
    // MOD/RM BITS
    bool sib;
    enum {
        mod_disp0,
        mod_disp8,
        mod_disp32,
        mod_reg,
    } type;
    struct regptr modrm_reg;

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
inline struct modrm_info modrm_get_info(byte_t byte) {
    return modrm_table[byte];
}
#endif

// Decodes ModR/M and SIB byte pointed to by cpu->eip, increments cpu->eip past
// them, and returns everything in out parameters.
// TODO currently only does 32-bit
inline void modrm_decode32(struct cpu_state *cpu, addr_t *addr_out, struct modrm_info *info_out) {
    struct modrm_info info = modrm_get_info(MEM_GET(cpu, cpu->eip, 8));
    cpu->eip++;
    *info_out = info;
    if (info.type == mod_reg) return;
    *addr_out = 0;

    if (!info.sib) {
        if (info.modrm_reg.reg32_id != 0) {
            *addr_out += REG_VAL(cpu, info.modrm_reg.reg32_id, 32);
        }
    } else {
        TODO("decode SIB");
    }

    switch (info.type) {
        case mod_disp8:
            *addr_out += MEM_GET(cpu, cpu->eip, 8);
            cpu->eip++;
            break;
        case mod_disp32:
            *addr_out += MEM_GET(cpu, cpu->eip, 32);
            cpu->eip += 4;
            break;

        // shut up compiler I don't want to handle other cases
        default:;
    }
}
