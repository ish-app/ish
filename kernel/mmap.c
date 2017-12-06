#include "debug.h"
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "kernel/process.h"
#include "emu/memory.h"

addr_t sys_mmap2(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset) {
    return sys_mmap(addr, len, prot, flags, fd_no, offset << PAGE_BITS);
}

addr_t sys_mmap(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset) {
    STRACE("mmap(0x%x, 0x%x, 0x%x, 0x%x, %d, %d)", addr, len, prot, flags, fd_no, offset);
    int err;

    if (len == 0)
        return _EINVAL;
    if (prot & ~(P_READ | P_WRITE | P_EXEC))
        return _EINVAL;

    pages_t pages = PAGE_ROUND_UP(len);
    page_t page;
    if (addr == 0) {
        page = pt_find_hole(current->cpu.mem, pages);
        if (page == BAD_PAGE)
            return _ENOMEM;
    } else {
        if (OFFSET(addr) != 0)
            return _EINVAL;
        page = PAGE(addr);
    }
    if (flags & MMAP_ANONYMOUS) {
        if (!(flags & MMAP_PRIVATE)) {
            TODO("MMAP_SHARED");
            return _EINVAL;
        }
        if ((err = pt_map_nothing(current->cpu.mem, page, pages, prot)) < 0)
            return err;
    } else {
        // fd must be valid
        struct fd *fd = current->files[fd_no];
        if (fd == NULL)
            return _EBADF;
        if (fd->ops->mmap == NULL)
            return _ENODEV;
        if ((err = fd->ops->mmap(fd, current->cpu.mem, page, pages, offset, prot, flags)) < 0)
            return err;
    }
    return page << PAGE_BITS;
}

int_t sys_munmap(addr_t addr, uint_t len) {
    if (OFFSET(addr) != 0)
        return _EINVAL;
    if (len == 0)
        return _EINVAL;
    if (pt_unmap(current->cpu.mem, PAGE(addr), PAGE_ROUND_UP(len), 0) < 0)
        return _EINVAL;
    return 0;
}

int_t sys_mprotect(addr_t addr, uint_t len, int_t prot) {
    STRACE("mprotect(0x%x, 0x%x, 0x%x)", addr, len, prot);
    if (OFFSET(addr) != 0)
        return _EINVAL;
    if (prot & ~(P_READ | P_WRITE | P_EXEC))
        return _EINVAL;
    pages_t pages = PAGE_ROUND_UP(len);
    return pt_set_flags(current->cpu.mem, PAGE(addr), pages, prot);
}

dword_t sys_madvise(addr_t addr, dword_t len, dword_t advice) {
    // portable applications should not rely on linux's destructive semantics for MAVD_DONTNEED.
    return 0;
}
