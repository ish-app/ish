#include <sys/mman.h>
#include <string.h>
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "emu/memory.h"

addr_t sys_brk(addr_t new_brk) {
    STRACE("brk(0x%x)", new_brk);
    int err;

    if (new_brk != 0) {
        if (new_brk < current->start_brk) return _EINVAL;
        // TODO check for not going too high

        addr_t old_brk = current->brk;
        if (new_brk > old_brk) {
            // expand heap: map region from old_brk to new_brk
            err = pt_map_nothing(current->cpu.mem, PAGE_ROUND_UP(old_brk),
                    PAGE_ROUND_UP(new_brk) - PAGE_ROUND_UP(old_brk), P_WRITE);
            if (err < 0) return err;
        } else if (new_brk < old_brk) {
            // shrink heap: unmap region from new_brk to old_brk
            // first page to unmap is PAGE(new_brk);
            // last page to unmap is PAGE(old_brk)
            pt_unmap(current->cpu.mem, PAGE(new_brk), PAGE(old_brk), PT_FORCE);
        }
        current->brk = new_brk;
    }
    return current->brk;
}
