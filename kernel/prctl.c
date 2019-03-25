#include "kernel/calls.h"

#define PRCTL_SET_KEEPCAPS_ 8

int_t sys_prctl(dword_t option, uint_t UNUSED(arg2), uint_t UNUSED(arg3), uint_t UNUSED(arg4), uint_t UNUSED(arg5)) {
    switch (option) {
        case PRCTL_SET_KEEPCAPS_:
            // stub
            return 0;
        default:
            STRACE("prctl(%#x)", option);
            return _EINVAL;
    }
}
