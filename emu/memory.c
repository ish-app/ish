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
#include "emu/memory.h"
#include "jit/jit.h"

// increment the change count
static void mem_changed(struct mem *mem);

void mem_init(struct mem *mem) {
    mem->pgdir = calloc(MEM_PGDIR_SIZE, sizeof(struct pt_entry *));
    mem->pgdir_used = 0;
    mem->changes = 0;
#if JIT
    mem->jit = jit_new(mem);
#endif
    wrlock_init(&mem->lock);
}

void mem_destroy(struct mem *mem) {
    write_wrlock(&mem->lock);
    pt_unmap(mem, 0, MEM_PAGES, PT_FORCE);
#if JIT
    jit_free(mem->jit);
#endif
    for (int i = 0; i < MEM_PGDIR_SIZE; i++) {
        if (mem->pgdir[i] != NULL)
            free(mem->pgdir[i]);
    }
    free(mem->pgdir);
    write_wrunlock(&mem->lock);
    wrlock_destroy(&mem->lock);
}

#define PGDIR_TOP(page) ((page) >> 10)
#define PGDIR_BOTTOM(page) ((page) & (MEM_PGDIR_SIZE - 1))

static struct pt_entry *mem_pt_new(struct mem *mem, page_t page) {
    struct pt_entry *pgdir = mem->pgdir[PGDIR_TOP(page)];
    if (pgdir == NULL) {
        pgdir = mem->pgdir[PGDIR_TOP(page)] = calloc(MEM_PGDIR_SIZE, sizeof(struct pt_entry));
        mem->pgdir_used++;
    }
    return &pgdir[PGDIR_BOTTOM(page)];
}

struct pt_entry *mem_pt(struct mem *mem, page_t page) {
    struct pt_entry *pgdir = mem->pgdir[PGDIR_TOP(page)];
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

static void next_page(struct mem *mem, page_t *page) {
    (*page)++;
    if (*page >= MEM_PAGES)
        return;
    while (*page < MEM_PAGES && mem->pgdir[PGDIR_TOP(*page)] == NULL)
        *page = (*page - PGDIR_BOTTOM(*page)) + MEM_PGDIR_SIZE;
}

page_t pt_find_hole(struct mem *mem, pages_t size) {
    page_t hole_end;
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
    return BAD_PAGE;
}

bool pt_is_hole(struct mem *mem, page_t start, pages_t pages) {
    for (page_t page = start; page < start + pages; page++) {
        if (mem_pt(mem, page) != NULL)
            return false;
    }
    return true;
}

int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, unsigned flags) {
    if (memory == MAP_FAILED)
        return errno_map();

    struct data *data = malloc(sizeof(struct data));
    if (data == NULL)
        return _ENOMEM;
    data->data = memory;
    data->size = pages * PAGE_SIZE;
    data->refcount = 0;

    for (page_t page = start; page < start + pages; page++) {
        if (mem_pt(mem, page) != NULL)
            pt_unmap(mem, page, 1, 0);
        data->refcount++;
        struct pt_entry *pt = mem_pt_new(mem, page);
        pt->data = data;
        pt->offset = (page - start) << PAGE_BITS;
        pt->flags = flags;
    }
    return 0;
}

int pt_unmap(struct mem *mem, page_t start, pages_t pages, int force) {
    if (!force)
        for (page_t page = start; page < start + pages; page++)
            if (mem_pt(mem, page) == NULL)
                return -1;

    for (page_t page = start; page < start + pages; next_page(mem, &page)) {
        struct pt_entry *pt = mem_pt(mem, page);
        if (pt == NULL)
            continue;
#if JIT
        jit_invalidate_page(mem->jit, page);
#endif
        struct data *data = pt->data;
        mem_pt_del(mem, page);
        if (--data->refcount == 0) {
            munmap(data->data, data->size);
            free(data);
        }
    }
    mem_changed(mem);
    return 0;
}

int pt_map_nothing(struct mem *mem, page_t start, pages_t pages, unsigned flags) {
    if (pages == 0) return 0;
    void *memory = mmap(NULL, pages * PAGE_SIZE,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    return pt_map(mem, start, pages, memory, flags | P_ANONYMOUS);
}

int pt_set_flags(struct mem *mem, page_t start, pages_t pages, int flags) {
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
    for (page_t page = start; page < start + pages; next_page(src, &page)) {
        struct pt_entry *entry = mem_pt(src, page);
        if (entry == NULL)
            continue;
        if (pt_unmap(dst, page, 1, PT_FORCE) < 0)
            return -1;
        if (!(entry->flags & P_SHARED))
            entry->flags |= P_COW;
        entry->flags &= ~P_COMPILED;
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
    mem->changes++;
}

void *mem_ptr(struct mem *mem, addr_t addr, int type) {
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
        pt_map_nothing(mem, page, 1, P_WRITE | P_GROWSDOWN);
        entry = mem_pt(mem, page);
    }

    if (entry != NULL && type == MEM_WRITE) {
        // if page is unwritable, well tough luck
        if (!(entry->flags & P_WRITE))
            return NULL;
        // if page is cow, ~~milk~~ copy it
        if (entry->flags & P_COW) {
            void *data = (char *) entry->data->data + entry->offset;
            void *copy = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            memcpy(copy, data, PAGE_SIZE);
            pt_map(mem, page, 1, copy, entry->flags &~ P_COW);
        }
#if JIT
        // get rid of any compiled blocks in this page
        jit_invalidate_page(mem->jit, page);
#endif
    }

    if (entry == NULL)
        return NULL;
    return entry->data->data + entry->offset + PGOFFSET(addr);
}

int mem_segv_reason(struct mem *mem, addr_t addr, int type) {
    assert(type == MEM_READ || type == MEM_WRITE);
    struct pt_entry *pt = mem_pt(mem, PAGE(addr));
    if (pt == NULL)
        return SEGV_MAPERR_;
    if ((type == MEM_READ && !(pt->flags & P_READ)) ||
            (type == MEM_WRITE && !(pt->flags & P_WRITE)))
        return SEGV_ACCERR_;
    die("caught segv for valid access");
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
    if (ftruncate(fd, 0xffffffff) < 0) {
        perror("ftruncate");
        return;
    }

    int pages = 0;
    for (page_t page = 0; page < MEM_PAGES; page++) {
        struct pt_entry *entry = mem_pt(mem, page);
        if (entry == NULL)
            continue;
        pages++;
        if (lseek(fd, page << PAGE_BITS, SEEK_SET) < 0) {
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
