#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define DEFAULT_CHANNEL memory
#include "debug.h"
#include "kernel/errno.h"
#include "emu/memory.h"

// increment the change count
static void mem_changed(struct mem *mem);

struct mem *mem_new() {
    struct mem *mem = malloc(sizeof(struct mem));
    if (mem == NULL)
        return NULL;
    mem->refcount = 1;
    mem->pt = calloc(MEM_PAGES, sizeof(struct pt_entry));
    mem->changes = 0;
    wrlock_init(&mem->lock);
    return mem;
}

void mem_retain(struct mem *mem) {
    mem->refcount++;
}

void mem_release(struct mem *mem) {
    if (--mem->refcount == 0) {
        write_wrlock(&mem->lock);
        pt_unmap(mem, 0, MEM_PAGES, PT_FORCE);
        free(mem->pt);
        write_wrunlock(&mem->lock);
        free(mem);
    }
}

page_t pt_find_hole(struct mem *mem, pages_t size) {
    page_t hole_end;
    bool in_hole = false;
    for (page_t page = 0xf7ffd; page > 0x40000; page--) {
        // I don't know how this works but it does
        if (!in_hole && mem->pt[page].data == NULL) {
            in_hole = true;
            hole_end = page + 1;
        }
        if (mem->pt[page].data != NULL)
            in_hole = false;
        else if (hole_end - page == size)
            return page;
    }
    return BAD_PAGE;
}

int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, unsigned flags) {
    if (memory == MAP_FAILED)
        return errno_map();

    struct data *data = malloc(sizeof(struct data));
    if (data == NULL)
        return _ENOMEM;
    data->data = memory;
    data->refcount = 0;

    for (page_t page = start; page < start + pages; page++) {
        if (mem->pt[page].data != NULL)
            pt_unmap(mem, page, 1, 0);
        data->refcount++;
        mem->pt[page].data = data;
        mem->pt[page].offset = (page - start) << PAGE_BITS;
        mem->pt[page].flags = flags;
    }
    mem_changed(mem);
    return 0;
}

int pt_unmap(struct mem *mem, page_t start, pages_t pages, int force) {
    if (!force)
        for (page_t page = start; page < start + pages; page++)
            if (mem->pt[page].data == NULL)
                return -1;

    for (page_t page = start; page < start + pages; page++) {
        if (mem->pt[page].data != NULL) {
            struct data *data = mem->pt[page].data;
            mem->pt[page].data = NULL;
            if (--data->refcount == 0) {
                munmap(data->data, PAGE_SIZE);
                free(data);
            }
        }
    }
    mem_changed(mem);
    return 0;
}

int pt_map_nothing(struct mem *mem, page_t start, pages_t pages, unsigned flags) {
    if (pages == 0) return 0;
    void *memory = mmap(NULL, pages * PAGE_SIZE,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    return pt_map(mem, start, pages, memory, flags);
}

int pt_set_flags(struct mem *mem, page_t start, pages_t pages, int flags) {
    for (page_t page = start; page < start + pages; page++)
        if (mem->pt[page].data == NULL)
            return _ENOMEM;
    for (page_t page = start; page < start + pages; page++)
        mem->pt[page].flags = flags;
    mem_changed(mem);
    return 0;
}

int pt_copy_on_write(struct mem *src, page_t src_start, struct mem *dst, page_t dst_start, page_t pages) {
    for (page_t src_page = src_start, dst_page = dst_start;
            src_page < src_start + pages;
            src_page++, dst_page++) {
        if (src->pt[src_page].data != NULL) {
            if (pt_unmap(dst, dst_page, 1, PT_FORCE) < 0)
                return -1;
            // TODO skip shared mappings
            struct pt_entry *entry = &src->pt[src_page];
            entry->flags |= P_COW;
            entry->data->refcount++;
            dst->pt[dst_page] = *entry;
        }
    }
    mem_changed(src);
    mem_changed(dst);
    return 0;
}

static void mem_changed(struct mem *mem) {
    mem->changes++;
}

static __no_instrument void set_dirty_page(struct mem *mem, page_t page) {
    mem->dirty_page = page;
}

void *mem_ptr(struct mem *mem, addr_t addr, int type) {
    page_t page = PAGE(addr);
    struct pt_entry *entry = &mem->pt[page];

    if (entry->data == NULL) {
        // page does not exist
        // look to see if the next VM region is willing to grow down
        page_t p = page + 1;
        while (p < MEM_PAGES && mem->pt[p].data == NULL)
            p++;
        if (p >= MEM_PAGES)
            return NULL;
        if (!(mem->pt[p].flags & P_GROWSDOWN))
            return NULL;
        pt_map_nothing(mem, page, 1, P_WRITE | P_GROWSDOWN);
    }

    if (type == MEM_WRITE && !P_WRITABLE(entry->flags)) {
        // page is unwritable or cow
        // if page is cow, ~~milk~~ copy it
        if (entry->flags & P_COW) {
            void *data = (char *) entry->data->data + entry->offset;
            void *copy = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, 
                    MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            memcpy(copy, data, PAGE_SIZE);
            pt_map(mem, page, 1, copy, entry->flags &~ P_COW);
        } else {
            return NULL;
        }
    }

    if (entry->data == NULL)
        return NULL;
    set_dirty_page(mem, addr & 0xfffff000);
    return entry->data->data + entry->offset + OFFSET(addr);
}

size_t real_page_size;
__attribute__((constructor)) static void get_real_page_size() {
    real_page_size = sysconf(_SC_PAGESIZE);
}
