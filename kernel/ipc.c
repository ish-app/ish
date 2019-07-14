#include "kernel/calls.h"

int_t sys_ipc(uint_t call, int_t first, int_t second, int_t third, addr_t ptr, int_t fifth) {
    STRACE("ipc(%u, %d, %d, %d, %#x, %d)", call, first, second, third, ptr, fifth);
    return _ENOSYS;
}
