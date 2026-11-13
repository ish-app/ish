#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define DEFAULT_CHANNEL memory
#include "debug.h"
#include "kernel/errno.h"
#include "kernel/signal.h"
#include "kernel/memory.h"
#include "asbestos/asbestos.h"
#include "kernel/vdso.h"
#include "kernel/task.h"
#include "fs/fd.h"

// increment the change count
static void mem_changed(struct mem *mem);
static struct mmu_ops mem_mmu_ops;

// Allocate host memory for anonymous guest pages.
//
// When the unicorn reference engine is in use (mem_anon_hint), the memory is
// requested at host_base + guest address, so pages that are neighbors in the
// guest are normally neighbors in the host too. Unicorn can then take long
// runs of pages as a single region (it is slow and limited when it has many
// regions). The address is only a preference: if the spot is taken the kernel
// is left to choose.
bool mem_anon_hint = false;

static void *mem_anon_alloc(struct mem *mem, page_t start, pages_t pages) {
    size_t size = (size_t) pages * PAGE_SIZE;
#if ISH_GUEST_64
    // guest memory is only committed when touched, however much is mapped
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
#if defined(MAP_FIXED_NOREPLACE)
    // Only for the reference CPU. Left to itself the host kernel puts new
    // mappings next to each other and merges them, which keeps the number of
    // host mappings low; fixed spots defeat that.
    if (mem_anon_hint && (qword_t) start + pages <= 0x100000) {
        void *want = (void *) (mem->host_base + ((uintptr_t) start << PAGE_BITS));
        void *got = mmap(want, size, PROT_READ | PROT_WRITE, flags | MAP_FIXED_NOREPLACE, -1, 0);
        if (got == want)
            return got;
        if (got != MAP_FAILED)
            munmap(got, size); // an old kernel ignored the flag
    }
#endif
    return mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);
#else
    (void) mem; (void) start;
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
}

void mem_init(struct mem *mem) {
#if ISH_GUEST_64
    // a 4 GB window per address space, spread over a part of the host address
    // space nothing else uses
    static atomic_uint next_window;
    mem->host_base = 0x100000000000ul + (uintptr_t) (atomic_fetch_add(&next_window, 1) % 4096) * 0x100000000ul;
#endif
    mem->pgdir = calloc(MEM_TOP_SIZE, sizeof(*mem->pgdir));
    mem->pgdir_used = 0;
    mem->mmu.ops = &mem_mmu_ops;
    mem->mmu.asbestos = asbestos_new(&mem->mmu);
    mem->mmu.changes = 0;
    wrlock_init(&mem->lock);
}

void mem_destroy(struct mem *mem) {
    write_wrlock(&mem->lock);
    pt_unmap_always(mem, 0, MEM_PAGES);
    asbestos_free(mem->mmu.asbestos);
    for (unsigned i = 0; i < MEM_TOP_SIZE; i++) {
        if (mem->pgdir[i] == NULL)
            continue;
#if ISH_GUEST_64
        for (unsigned j = 0; j < MEM_PGDIR_SIZE; j++)
            free(mem->pgdir[i][j]);
#endif
        free(mem->pgdir[i]);
    }
    free(mem->pgdir);
    write_wrunlock(&mem->lock);
    wrlock_destroy(&mem->lock);
}

#define PGDIR_BOTTOM(page) ((page) & (MEM_PGDIR_SIZE - 1))

// The table of MEM_PGDIR_SIZE entries that holds this page's entry, or NULL.
static inline struct pt_entry *mem_leaf(struct mem *mem, page_t page, bool create) {
#if ISH_GUEST_64
    if (page >= MEM_PAGES)
        return NULL;
    struct pt_entry **mid = mem->pgdir[page >> 20];
    if (mid == NULL) {
        if (!create)
            return NULL;
        mid = mem->pgdir[page >> 20] = calloc(MEM_PGDIR_SIZE, sizeof(struct pt_entry *));
    }
    struct pt_entry **slot = &mid[(page >> 10) & (MEM_PGDIR_SIZE - 1)];
#else
    struct pt_entry **slot = &mem->pgdir[page >> 10];
#endif
    if (*slot == NULL && create) {
        *slot = calloc(MEM_PGDIR_SIZE, sizeof(struct pt_entry));
        mem->pgdir_used++;
    }
    return *slot;
}

static struct pt_entry *mem_pt_new(struct mem *mem, page_t page) {
    return &mem_leaf(mem, page, true)[PGDIR_BOTTOM(page)];
}

struct pt_entry *mem_pt(struct mem *mem, page_t page) {
    struct pt_entry *pgdir = mem_leaf(mem, page, false);
    if (pgdir == NULL)
        return NULL;
    struct pt_entry *entry = &pgdir[PGDIR_BOTTOM(page)];
    if (entry->data == NULL)
        return NULL;
    return entry;
}

static void mem_pt_del(struct mem *mem, page_t page) {
    struct pt_entry *entry = mem_pt(mem, page);
    if (entry != NULL)
        entry->data = NULL;
}

void mem_next_page(struct mem *mem, page_t *page) {
    (*page)++;
    while (*page < MEM_PAGES) {
#if ISH_GUEST_64
        if (mem->pgdir[*page >> 20] == NULL) {
            *page = ((*page >> 20) + 1) << 20;
            continue;
        }
#endif
        if (mem_leaf(mem, *page, false) != NULL)
            break;
        *page = (*page - PGDIR_BOTTOM(*page)) + MEM_PGDIR_SIZE;
    }
}

// Is [start, start + pages) inside the address space?
static inline bool pt_range_ok(page_t start, pages_t pages) {
    return start < MEM_PAGES && pages <= MEM_PAGES - start;
}

page_t pt_find_hole(struct mem *mem, pages_t size) {
    page_t hole_end = 0; // this can never be used before initializing but gcc doesn't realize
    bool in_hole = false;
    for (page_t page = 0xf7ffd; page > 0x40000; page--) {
        // I don't know how this works but it does
        if (!in_hole && mem_pt(mem, page) == NULL) {
            in_hole = true;
            hole_end = page + 1;
        }
        if (mem_pt(mem, page) != NULL)
            in_hole = false;
        else if (hole_end - page == size)
            return page;
    }
#if ISH_GUEST_64
    // Nothing that large is free below 4 GB: take the lowest hole above it.
    page_t start = 0x100000;
    while (pt_range_ok(start, size)) {
        // find the first mapped page in [start, start + size)
        page_t page = start;
        if (mem_pt(mem, page) == NULL) {
            // mem_next_page stops at pages whose table exists, mapped or not
            do {
                mem_next_page(mem, &page);
            } while (page < MEM_PAGES && page - start < size && mem_pt(mem, page) == NULL);
            if (page >= MEM_PAGES || page - start >= size)
                return start;
        }
        // skip the mapping that is in the way
        while (page < MEM_PAGES && mem_pt(mem, page) != NULL)
            page++;
        start = page;
    }
#endif
    return BAD_PAGE;
}

bool pt_is_hole(struct mem *mem, page_t start, pages_t pages) {
    if (!pt_range_ok(start, pages))
        return false;
    for (page_t page = start; page < start + pages; mem_next_page(mem, &page)) {
        if (mem_pt(mem, page) != NULL)
            return false;
    }
    return true;
}

int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, size_t offset, unsigned flags) {
    if (memory == MAP_FAILED)
        return errno_map();
    if (!pt_range_ok(start, pages)) {
        if (memory != vdso_data)
            munmap(memory, (size_t) pages * PAGE_SIZE + offset);
        return _ENOMEM;
    }

    // If this fails, the munmap in pt_unmap would probably fail.
    assert((uintptr_t) memory % real_page_size == 0 || memory == vdso_data);

    struct data *data = malloc(sizeof(struct data));
    if (data == NULL)
        return _ENOMEM;
    *data = (struct data) {
        .data = memory,
        .size = (size_t) pages * PAGE_SIZE + offset,

#if LEAK_DEBUG
        .pid = current ? current->pid : 0,
        .dest = PAGE_ADDR(start),
#endif
    };

    for (page_t page = start; page < start + pages; page++) {
        if (mem_pt(mem, page) != NULL)
            pt_unmap(mem, page, 1);
        data->refcount++;
        struct pt_entry *pt = mem_pt_new(mem, page);
        pt->data = data;
        pt->offset = ((size_t) (page - start) << PAGE_BITS) + offset;
        pt->flags = flags;
    }
    return 0;
}

int pt_unmap(struct mem *mem, page_t start, pages_t pages) {
    if (!pt_range_ok(start, pages))
        return -1;
    for (page_t page = start; page < start + pages; page++)
        if (mem_pt(mem, page) == NULL)
            return -1;
    return pt_unmap_always(mem, start, pages);
}

int pt_unmap_always(struct mem *mem, page_t start, pages_t pages) {
    if (start >= MEM_PAGES)
        return 0;
    if (pages > MEM_PAGES - start)
        pages = MEM_PAGES - start;
    // (start may be in a table that does not exist: mem_pt copes)
    for (page_t page = start; page < start + pages; mem_next_page(mem, &page)) {
        struct pt_entry *pt = mem_pt(mem, page);
        if (pt == NULL)
            continue;
        asbestos_invalidate_page(mem->mmu.asbestos, page);
        struct data *data = pt->data;
        mem_pt_del(mem, page);
        if (--data->refcount == 0) {
            // vdso wasn't allocated with mmap, it's just in our data segment
            if (data->data != vdso_data) {
                int err = munmap(data->data, data->size);
                if (err != 0)
                    die("munmap(%p, %lu) failed: %s", data->data, data->size, strerror(errno));
            }
            if (data->fd != NULL) {
                fd_close(data->fd);
            }
            free(data);
        }
    }
    mem_changed(mem);
    return 0;
}

int pt_map_nothing(struct mem *mem, page_t start, pages_t pages, unsigned flags) {
    if (pages == 0) return 0;
    if (!pt_range_ok(start, pages))
        return _ENOMEM;
    void *memory = mem_anon_alloc(mem, start, pages);
    return pt_map(mem, start, pages, memory, 0, flags | P_ANONYMOUS);
}

int pt_set_flags(struct mem *mem, page_t start, pages_t pages, int flags) {
    if (!pt_range_ok(start, pages))
        return _ENOMEM;
    for (page_t page = start; page < start + pages; page++)
        if (mem_pt(mem, page) == NULL)
            return _ENOMEM;
    for (page_t page = start; page < start + pages; page++) {
        struct pt_entry *entry = mem_pt(mem, page);
        int old_flags = entry->flags;
        entry->flags = flags;
        // check if protection is increasing
        if ((flags & ~old_flags) & (P_READ|P_WRITE)) {
            void *data = (char *) entry->data->data + entry->offset;
            // force to be page aligned
            data = (void *) ((uintptr_t) data & ~(real_page_size - 1));
            int prot = PROT_READ;
            if (flags & P_WRITE) prot |= PROT_WRITE;
            if (mprotect(data, real_page_size, prot) < 0)
                return errno_map();
        }
    }
    mem_changed(mem);
    return 0;
}

int pt_copy_on_write(struct mem *src, struct mem *dst, page_t start, page_t pages) {
    for (page_t page = start; page < start + pages; mem_next_page(src, &page)) {
        struct pt_entry *entry = mem_pt(src, page);
        if (entry == NULL)
            continue;
        if (pt_unmap_always(dst, page, 1) < 0)
            return -1;
        if (!(entry->flags & P_SHARED))
            entry->flags |= P_COW;
        entry->data->refcount++;
        struct pt_entry *dst_entry = mem_pt_new(dst, page);
        dst_entry->data = entry->data;
        dst_entry->offset = entry->offset;
        dst_entry->flags = entry->flags;
    }
    mem_changed(src);
    mem_changed(dst);
    return 0;
}

static void mem_changed(struct mem *mem) {
    mem->mmu.changes++;
}

// This version will return NULL instead of making necessary pagetable changes.
// Used by the emulator to avoid deadlocks.
static void *mem_ptr_nofault(struct mem *mem, addr_t addr, int type) {
    struct pt_entry *entry = mem_pt(mem, PAGE(addr));
    if (entry == NULL)
        return NULL;
    if (type == MEM_WRITE && !P_WRITABLE(entry->flags))
        return NULL;
    return entry->data->data + entry->offset + PGOFFSET(addr);
}

void *mem_ptr(struct mem *mem, addr_t addr, int type) {
    void *old_ptr = mem_ptr_nofault(mem, addr, type); // just for an assert

    page_t page = PAGE(addr);
    struct pt_entry *entry = mem_pt(mem, page);

    if (entry == NULL) {
        // page does not exist
        // look to see if the next VM region is willing to grow down
        page_t p = page + 1;
        while (p < MEM_PAGES && mem_pt(mem, p) == NULL)
            p++;
        if (p >= MEM_PAGES)
            return NULL;
        if (!(mem_pt(mem, p)->flags & P_GROWSDOWN))
            return NULL;

        // Changing memory maps must be done with the write lock. But this is
        // called with the read lock.
        // This locking stuff is copy/pasted for all the code in this function
        // which changes memory maps.
        // TODO: factor the lock/unlock code here into a new function. Do this
        // next time you touch this function.
        read_wrunlock(&mem->lock);
        write_wrlock(&mem->lock);
        // (unless another thread grew the stack here in the meantime)
        if (mem_pt(mem, page) == NULL)
            pt_map_nothing(mem, page, 1, P_WRITE | P_GROWSDOWN);
        write_wrunlock(&mem->lock);
        read_wrlock(&mem->lock);

        entry = mem_pt(mem, page);
    }

    if (entry != NULL && (type == MEM_WRITE || type == MEM_WRITE_PTRACE)) {
        // if page is unwritable, well tough luck
        if (type != MEM_WRITE_PTRACE && !(entry->flags & P_WRITE))
            return NULL;
        if (type == MEM_WRITE_PTRACE) {
            // TODO: Is P_WRITE really correct? The page shouldn't be writable without ptrace.
            entry->flags |= P_WRITE | P_COW;
        }
        // get rid of any compiled blocks in this page
        asbestos_invalidate_page(mem->mmu.asbestos, page);
        // if page is cow, ~~milk~~ copy it
        if (entry->flags & P_COW) {
            void *copy = mem_anon_alloc(mem, page, 1);
            if (copy == MAP_FAILED)
                return NULL;

            // copy/paste from above
            read_wrunlock(&mem->lock);
            write_wrlock(&mem->lock);
            // Another thread of this address space may have copied the page
            // (and released the original) while no lock was held: look again.
            entry = mem_pt(mem, page);
            if (entry != NULL && (entry->flags & P_COW)) {
                void *data = (char *) entry->data->data + entry->offset;
                memcpy(copy, data, PAGE_SIZE);
                pt_map(mem, page, 1, copy, 0, entry->flags &~ P_COW);
            } else {
                munmap(copy, PAGE_SIZE);
            }
            write_wrunlock(&mem->lock);
            read_wrlock(&mem->lock);
        }
    }

    void *ptr = mem_ptr_nofault(mem, addr, type);
    assert(old_ptr == NULL || old_ptr == ptr || type == MEM_WRITE_PTRACE);
    return ptr;
}

static void *mem_mmu_translate(struct mmu *mmu, addr_t addr, int type) {
    return mem_ptr_nofault(container_of(mmu, struct mem, mmu), addr, type);
}

static struct mmu_ops mem_mmu_ops = {
    .translate = mem_mmu_translate,
};

int mem_segv_reason(struct mem *mem, addr_t addr) {
    struct pt_entry *pt = mem_pt(mem, PAGE(addr));
    if (pt == NULL)
        return SEGV_MAPERR_;
    return SEGV_ACCERR_;
}

size_t real_page_size;
__attribute__((constructor)) static void get_real_page_size() {
    real_page_size = sysconf(_SC_PAGESIZE);
}

void mem_coredump(struct mem *mem, const char *file) {
    int fd = open(file, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) {
        perror("open");
        return;
    }
    int pages = 0;
    for (page_t page = 0; page < MEM_PAGES; mem_next_page(mem, &page)) {
        struct pt_entry *entry = mem_pt(mem, page);
        if (entry == NULL)
            continue;
        pages++;
        if (lseek(fd, (off_t) page << PAGE_BITS, SEEK_SET) < 0) {
            perror("lseek");
            return;
        }
        if (write(fd, entry->data->data, PAGE_SIZE) < 0) {
            perror("write");
            return;
        }
    }
    printk("dumped %d pages\n", pages);
    close(fd);
}
