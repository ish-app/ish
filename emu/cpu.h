#ifndef EMU_H
#define EMU_H

#include <stddef.h>
#include "emu/types.h"
#include "emu/memory.h"

struct cpu_state {
    pagetable pt;

    // assumes little endian
#define _REG(n) \
    union { \
        dword_t e##n; \
        struct { \
            word_t n; \
        }; \
    };
#define _REGX(n) \
    union { \
        dword_t e##n##x; \
        struct { \
            word_t n##x; \
        }; \
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

    dword_t eip;

#undef REGX
#undef REG
};

typedef uint8_t reg_id_t;
#define REG_ID(reg) offsetof(struct cpu_state, reg)
#define REG_VAL(cpu, reg_id, size) (*((uint##size##_t *) (((char *) (cpu)) + reg_id)))

#define MEM_GET(cpu, addr, size) (((uint##size##_t *) cpu->pt[PAGE_ADDR(addr)]->data) [OFFSET_ADDR(addr)])

void cpu_run(struct cpu_state *cpu);

#endif
