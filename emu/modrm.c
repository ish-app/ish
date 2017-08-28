#include "emu/modrm.h"

#define MOD(byte) ((byte & 0b11000000) >> 6)
#define REG(byte) ((byte & 0b00111000) >> 3)
#define RM(byte)  ((byte & 0b00000111) >> 0)

#define MAKE_REGPTR(r32, r16, r8, xmm) ((struct regptr) { \
        .reg32_id = REG_ID(r32), \
        .reg16_id = REG_ID(r16), \
        .reg8_id = REG_ID(r8), \
        .reg128_id = REG_ID(xmm), \
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
    info.rm_opcode = RM(byte); // for floating point
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
                    info.modrm_regid = (struct regptr) {};
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

// Decodes ModR/M and SIB byte pointed to by cpu->eip, increments cpu->eip past
// them, and returns everything in out parameters.
// TODO currently only does 32-bit
void modrm_decode32(struct cpu_state *cpu, addr_t *addr_out, struct modrm_info *info_out) {
    byte_t modrm = *(byte_t *) mem_read_ptr(&cpu->mem, cpu->eip);
    struct modrm_info info = modrm_get_info(modrm);
    cpu->eip++;
    *info_out = info;
    if (info.type == mod_reg) return;

    if (!info.sib) {
        if (info.modrm_regid.reg32_id != 0) {
            *addr_out += REG_VAL(cpu, info.modrm_regid.reg32_id, 32);
        }
    } else {
        // sib is simple enough to not use a table for
        byte_t sib = *(byte_t *) mem_read_ptr(&cpu->mem, cpu->eip);
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
            disp = *(int8_t *) mem_read_ptr(&cpu->mem, cpu->eip);
            TRACE("disp %s0x%x ", (disp < 0 ? "-" : ""), (disp < 0 ? -disp : disp));
            *addr_out += disp;
            cpu->eip++;
            break;
        }
        case mod_disp32: {
            disp = *(int32_t *) mem_read_ptr(&cpu->mem, cpu->eip);
            TRACE("disp %s0x%x ", (disp < 0 ? "-" : ""), (disp < 0 ? -disp : disp));
            *addr_out += disp;
            cpu->eip += 4;
            break;
        }

        // shut up compiler I don't want to handle other cases
        default:;
    }
}
