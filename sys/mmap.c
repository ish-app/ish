#include "sys/calls.h"
#include "sys/errno.h"
#include "emu/process.h"
#include "emu/memory.h"

addr_t sys_mmap(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset) {
    int err;

    if (len == 0)
        return _EINVAL;
    if (prot & ~(P_READ | P_WRITE | P_EXEC))
        return _EINVAL;
    if (!(flags & MMAP_PRIVATE))
        // TODO MMAP_SHARED
        return _EINVAL;
    if (addr != 0)
        return _EINVAL;

    pages_t pages = PAGE_ROUND_UP(len);
    page_t page = pt_find_hole(&curmem, pages);
    if (page == BAD_PAGE)
        return _ENOMEM;
    if (flags & MMAP_ANONYMOUS) {
        if ((err = pt_map_nothing(&curmem, page, pages, prot)) < 0)
            return err;
    } else {
        // fd must be valid
        struct fd *fd = current->files[fd_no];
        if (fd == NULL)
            return _EBADF;
        if (fd->ops->mmap == NULL)
            return _ENODEV;
        void *memory;
        if ((err = fd->ops->mmap(fd, offset, len, prot, flags, &memory)) < 0)
            return err;
        if ((err = pt_map(&curmem, page, pages, memory, flags)) < 0)
            return err;
    }
    return page << PAGE_BITS;
}

