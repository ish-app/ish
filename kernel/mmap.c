#include "debug.h"
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "kernel/process.h"
#include "fs/fdtable.h"
#include "emu/memory.h"

addr_t sys_mmap2(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset) {
    return sys_mmap(addr, len, prot, flags, fd_no, offset << PAGE_BITS);
}

static addr_t do_mmap(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset) {
    int err;
    pages_t pages = PAGE_ROUND_UP(len);
    page_t page;
    if (addr == 0) {
        page = pt_find_hole(curmem, pages);
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
        if ((err = pt_map_nothing(curmem, page, pages, prot)) < 0)
            return err;
    } else {
        // fd must be valid
        struct fd *fd = f_get(fd_no);
        if (fd == NULL)
            return _EBADF;
        if (fd->ops->mmap == NULL)
            return _ENODEV;
        if ((err = fd->ops->mmap(fd, curmem, page, pages, offset, prot, flags)) < 0)
            return err;
    }
    return page << PAGE_BITS;
}

addr_t sys_mmap(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset) {
    STRACE("mmap(0x%x, 0x%x, 0x%x, 0x%x, %d, %d)", addr, len, prot, flags, fd_no, offset);
    if (len == 0)
        return _EINVAL;
    if (prot & ~(P_READ | P_WRITE | P_EXEC))
        return _EINVAL;

    write_wrlock(&curmem->lock);
    addr_t res = do_mmap(addr, len, prot, flags, fd_no, offset);
    write_wrunlock(&curmem->lock);
    return res;
}

int_t sys_munmap(addr_t addr, uint_t len) {
    if (OFFSET(addr) != 0)
        return _EINVAL;
    if (len == 0)
        return _EINVAL;
    write_wrlock(&curmem->lock);
    int err = pt_unmap(curmem, PAGE(addr), PAGE_ROUND_UP(len), 0);
    write_wrunlock(&curmem->lock);
    if (err < 0)
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
    write_wrlock(&curmem->lock);
    int err = pt_set_flags(curmem, PAGE(addr), pages, prot);
    write_wrunlock(&curmem->lock);
    return err;
}

dword_t sys_madvise(addr_t addr, dword_t len, dword_t advice) {
    // portable applications should not rely on linux's destructive semantics for MADV_DONTNEED.
    return 0;
}

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
