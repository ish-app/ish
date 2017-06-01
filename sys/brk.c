#include "sys/calls.h"
#include "sys/errno.h"
#include "emu/process.h"
#include "emu/memory.h"

addr_t sys_brk(addr_t new_brk) {
    int err;

    if (new_brk != 0) {
        if (new_brk < current->start_brk) return _EINVAL;
        // TODO check for not going too high

        addr_t old_brk = current->brk;
        if (new_brk > old_brk) {
            // expand heap: map region from old_brk to new_brk
            err = pt_map_nothing(curmem, PAGE_ROUND_UP(old_brk),
                    PAGE_ROUND_UP(new_brk) - PAGE_ROUND_UP(old_brk), P_WRITABLE);
            if (err < 0) return err;
        } else if (new_brk < old_brk) {
            // shrink heap: unmap region from new_brk to old_brk
            TODO("shrink heap");
        }
        current->brk = new_brk;
    }
    return current->brk;
}
