#include "kernel/calls.h"

int_t sys_prctl(dword_t UNUSED(option), uint_t UNUSED(arg2), uint_t UNUSED(arg3), uint_t UNUSED(arg4), uint_t UNUSED(arg5)) {
    return _EINVAL;
}
