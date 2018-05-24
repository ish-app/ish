#include "emu/modrm.h"
#include "undefined-flags.h"
#include "ptutil.h"

#define C (1 << 0)
#define P (1 << 2)
#define A (1 << 4)
#define Z (1 << 6)
#define S (1 << 7)
#define O (1 << 11)

int undefined_flags_mask(int pid, struct cpu_state *cpu) {
    addr_t ip = cpu->eip;
    byte_t opcode;
#define read(x) pt_readn(pid, ip++, &x, sizeof(x));
    read(opcode);
    switch (opcode) {
        // shift or rotate, of is undefined if shift count is greater than 1
        case 0x0f:
            read(opcode);
            switch(opcode) {
                case 0xac:
                case 0xad: {
                    ip++;
                    byte_t shift;
                    if (opcode == 0xad)
                        shift = cpu->cl;
                    else
                        read(shift);
                    if (shift == 1)
                        return A;
                    else if (shift > 1)
                        return O|A;
                    break;
                }
                case 0xaf: return S|Z|A|P; // imul
                case 0xbc: return O|S|A|P|C; // bsf
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
            byte_t shift_count;
            if (opcode == 0xd0 || opcode == 0xd1)
                shift_count = 1;
            else if (opcode == 0xd2 || opcode == 0xd3)
                shift_count = cpu->cl;
            else
                pt_readn(pid, ip++, &shift_count, sizeof(shift_count));
            if (shift_count % 32 == 0)
                return O|P; // please delete this as soon as you can
            if (shift_count > 1)
                return O;
            break;
        }

        case 0xf7: {
            // group 3
            byte_t modrm;
            read(modrm);
            switch (REG(modrm)) {
                case 4: return S|Z|A|P; // mul
            }
            break;
        }
    }
    return 0;
}
