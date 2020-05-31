#include "emu/modrm.h"
#include "undefined-flags.h"
#include "ptutil.h"

#define C (1 << 0)
#define P (1 << 2)
#define A (1 << 4)
#define Z (1 << 6)
#define S (1 << 7)
#define O (1 << 11)

int undefined_flags_mask(struct cpu_state *cpu, struct tlb *tlb) {
    addr_t ip = cpu->eip;
    byte_t opcode;
#define read(x) tlb_read(tlb, ip++, &x, sizeof(x));
skip:
    read(opcode);
    switch (opcode) {
        // shift or rotate, of is undefined if shift count is greater than 1
        case 0x0f:
            read(opcode);
            switch(opcode) {
                // shrd/shld
                case 0xa4:
                case 0xa5:
                case 0xac:
                case 0xad: {
                    ip++;
                    byte_t shift = -1;
                    if (opcode == 0xad)
                        shift = cpu->cl;
                    else
                        read(shift);
                    if (shift == 1)
                        return O|A;
                    else if (shift > 1)
                        return O|A;
                    break;
                }
                case 0xaf: return S|Z|A|P; // imul
                case 0xbd: case 0xbc: return O|S|A|P|C; // bsr/bsf
            }
            break;

        case 0x69:
        case 0x6b: return S|Z|A|P; // imul

        case 0xc0:
        case 0xc1:
        case 0xd0:
        case 0xd1:
        case 0xd2:
        case 0xd3: {
            ip++; // skip modrm
            byte_t shift_count = -1;
            if (opcode == 0xd0 || opcode == 0xd1)
                shift_count = 1;
            else if (opcode == 0xd2 || opcode == 0xd3)
                shift_count = cpu->cl;
            else
                read(shift_count);
            if (shift_count > 1)
                return O;
            break;
        }

        case 0xf6: case 0xf7: {
            // group 3
            byte_t modrm = -1;
            read(modrm);
            switch (REG(modrm)) {
                case 4: return S|Z|A|P; // mul
                case 5: return S|Z|A|P; // imul
            }
            break;
        }
        case 0x66: goto skip;
    }
    return 0;
}
