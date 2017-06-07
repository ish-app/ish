#include "sys/calls.h"
#include "sys/errno.h"
#include "emu/process.h"
#include "emu/memory.h"

addr_t sys_mmap(addr_t addr, dword_t len, dword_t prot, dword_t flags, dword_t fd, off_t offset) {
    int err;

    if (len == 0)
        return _EINVAL;
    if (prot & ~(P_READ | P_WRITE | P_EXEC))
        return _EINVAL;
    // all that works at the moment is mmap(NULL, len, prot, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0)
    if (flags != (MMAP_PRIVATE | MMAP_ANONYMOUS))
        return _EINVAL;
    if (addr != 0)
        return _EINVAL;

    pages_t pages = PAGE_ROUND_UP(len);
    page_t page = pt_find_hole(&curmem, pages);
    if (page == BAD_PAGE)
        return _ENOMEM;
    if ((err = pt_map_nothing(&curmem, page, pages, prot)) < 0)
        return err;
    return page << PAGE_BITS;
}

