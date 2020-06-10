#ifndef CPUID_H
#define CPUID_H

#include "misc.h"

static inline void do_cpuid(dword_t *eax, dword_t *ebx, dword_t *ecx, dword_t *edx) {
    dword_t leaf = *eax;
    switch (leaf) {
        case 0:
            *eax = 0x01; // we support barely anything
            *ebx = 0x756e6547; // Genu
            *edx = 0x49656e69; // ineI
            *ecx = 0x6c65746e; // ntel
            break;
        default: // if leaf is too high, use highest supported leaf
        case 1:
            *eax = 0x0; // say nothing about cpu model number
            *ebx = 0x0; // processor number 0, flushes 0 bytes on clflush
            *ecx = 0; // we support none of the features in ecx
            *edx = (1 << 0) // fpu
                | (1 << 15) // cmov
                | (1 << 23) // mmx
                | (1 << 26) // sse2
                ;
            break;
    }
}

#endif
