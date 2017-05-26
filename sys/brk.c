#include "sys/calls.h"
#include "emu/process.h"

addr_t sys_brk(addr_t new_brk) {
    if (new_brk != 0) {
        current->brk = new_brk;
    }
    return current->brk;
}
