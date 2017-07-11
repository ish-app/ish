#include "sys/calls.h"
#include "sys/errno.h"
#include "sys/process.h"
#include "emu/memory.h"

addr_t sys_mmap2(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset) {
    return sys_mmap(addr, len, prot, flags, fd_no, offset << PAGE_BITS);
}

addr_t sys_mmap(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset) {
    int err;

    if (len == 0)
        return _EINVAL;
    if (prot & ~(P_READ | P_WRITE | P_EXEC))
        return _EINVAL;
    if (!(flags & MMAP_PRIVATE)) {
        TODO("MMAP_SHARED");
        return _EINVAL;
    }

    pages_t pages = PAGE_ROUND_UP(len);
    page_t page;
    if (addr == 0) {
        page = pt_find_hole(&curmem, pages);
        if (page == BAD_PAGE)
            return _ENOMEM;
    } else {
        if (OFFSET(addr) != 0)
            return _EINVAL;
        page = PAGE(addr);
    }
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

int_t sys_munmap(addr_t addr, uint_t len) {
    if (OFFSET(addr) != 0)
        return _EINVAL;
    if (len == 0)
        return _EINVAL;
    if (pt_unmap(&curmem, PAGE(addr), PAGE_ROUND_UP(len), 0) < 0)
        return _EINVAL;
    return 0;
}

int_t sys_mprotect(addr_t addr, uint_t len, int_t prot) {
    if (OFFSET(addr) != 0)
        return _EINVAL;
    if (prot & ~(P_READ | P_WRITE | P_EXEC))
        return _EINVAL;
    pages_t pages = PAGE_ROUND_UP(len);
    return pt_set_flags(&curmem, PAGE(addr), pages, prot);
}
