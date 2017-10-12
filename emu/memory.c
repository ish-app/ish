#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "sys/errno.h"
#include "emu/memory.h"

static void tlb_flush(struct mem *mem);

// this code currently assumes the system page size is 4k

struct mem *mem_new() {
    struct mem *mem = malloc(sizeof(struct mem));
    mem->refcount = 1;
    mem->pt = calloc(PT_SIZE, sizeof(struct pt_entry *));
    mem->tlb = malloc(TLB_SIZE * sizeof(struct tlb_entry));
    tlb_flush(mem);
    return mem;
}

void mem_retain(struct mem *mem) {
    mem->refcount++;
}

void mem_release(struct mem *mem) {
    if (mem->refcount-- == 0) {
        pt_unmap(mem, 0, PT_SIZE, PT_FORCE);
        free(mem->pt);
        free(mem->tlb);
        free(mem);
    }
}

page_t pt_find_hole(struct mem *mem, pages_t size) {
    page_t hole_end;
    bool in_hole = false;
    for (page_t page = 0xf7ffd; page > 0x40000; page--) {
        // I don't know how this works but it does
        if (!in_hole && mem->pt[page] == NULL) {
            in_hole = true;
            hole_end = page + 1;
        }
        if (mem->pt[page] != NULL)
            in_hole = false;
        else if (hole_end - page == size)
            return page;
    }
    return BAD_PAGE;
}

int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, unsigned flags) {
    if (memory == MAP_FAILED) {
        return err_map(errno);
    }
    for (page_t page = start; page < start + pages; page++) {
        if (mem->pt[page] != NULL) {
            // FIXME this is probably wrong
            pt_unmap(mem, page, 1, 0);
        }
        struct pt_entry *entry = malloc(sizeof(struct pt_entry));
        // FIXME this could allocate some of the memory and then abort
        if (entry == NULL) return _ENOMEM;
        entry->data = memory;
        entry->refcount = 1;
        entry->flags = flags;
        mem->pt[page] = entry;
        memory = (char *) memory + PAGE_SIZE;
    }
    tlb_flush(mem);
    return 0;
}

int pt_unmap(struct mem *mem, page_t start, pages_t pages, int force) {
    if (!force)
        for (page_t page = start; page < start + pages; page++)
            if (mem->pt[page] == NULL)
                return -1;

    for (page_t page = start; page < start + pages; page++) {
        if (mem->pt[page] != NULL) {
            struct pt_entry *entry = mem->pt[page];
            mem->pt[page] = NULL;
            entry->refcount--;
            if (entry->refcount == 0) {
                munmap(entry->data, PAGE_SIZE);
                free(entry);
            }
        }
    }
    tlb_flush(mem);
    return 0;
}

int pt_map_nothing(struct mem *mem, page_t start, pages_t pages, unsigned flags) {
    if (pages == 0) return 0;
    void *memory = mmap(NULL, pages * PAGE_SIZE,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    return pt_map(mem, start, pages, memory, flags);
}


int pt_map_file(struct mem *mem, page_t start, pages_t pages, int fd, off_t off, unsigned flags) {
    if (pages == 0) return 0;
    void *memory = mmap(NULL, pages * PAGE_SIZE,
            PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, off);
    return pt_map(mem, start, pages, memory, flags);
}

// FIXME this can overwrite P_GROWSDOWN or P_GUARD
int pt_set_flags(struct mem *mem, page_t start, pages_t pages, int flags) {
    for (page_t page = start; page < start + pages; page++)
        if (mem->pt[page] == NULL)
            return _ENOMEM;
    for (page_t page = start; page < start + pages; page++) {
        mem->pt[page]->flags = flags;
    }
    tlb_flush(mem);
    return 0;
}

int pt_copy_on_write(struct mem *src, page_t src_start, struct mem *dst, page_t dst_start, page_t pages) {
    for (page_t src_page = src_start, dst_page = dst_start;
            src_page < src_start + pages;
            src_page++, dst_page++) {
        if (src->pt[src_page] != NULL) {
            if (pt_unmap(dst, dst_page, 1, PT_FORCE) < 0)
                return -1;
            struct pt_entry *entry = src->pt[src_page];
            entry->flags |= P_COW;
            entry->refcount++;
            dst->pt[dst_page] = entry;
        }
    }
    tlb_flush(src);
    tlb_flush(dst);
    return 0;
}

static void tlb_flush(struct mem *mem) {
    memset(mem->tlb, 0, TLB_SIZE * sizeof(struct tlb_entry));
    for (unsigned i = 0; i < TLB_SIZE; i++) {
        mem->tlb[i].page = mem->tlb[i].page_if_writable = TLB_PAGE_EMPTY;
    }
}

void *tlb_handle_miss(struct mem *mem, addr_t addr, int type) {
    struct pt_entry *pt = mem->pt[PAGE(addr)];

    if (pt == NULL) {
        // page does not exist
        // look to see if the next VM region is willing to grow down
        page_t page = PAGE(addr) + 1;
        while (mem->pt[page] == NULL) {
            if (page >= PT_SIZE)
                return NULL;
            page++;
        }
        if (!(mem->pt[page]->flags & P_GROWSDOWN))
            return NULL;
        pt_map_nothing(mem, PAGE(addr), 1, P_WRITE | P_GROWSDOWN);
        pt = mem->pt[PAGE(addr)];
    }

    if (type == TLB_WRITE && !P_WRITABLE(pt->flags)) {
        // page is unwritable or cow
        // if page is cow, ~~milk~~ copy it
        if (pt->flags & P_COW) {
            void *data = pt->data;
            void *copy = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, 
                    MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            memcpy(copy, data, PAGE_SIZE);
            pt_map(mem, PAGE(addr), 1, copy, pt->flags &~ P_COW);
            pt = mem->pt[PAGE(addr)];
        } else {
            return NULL;
        }
    }

    // TODO if page is unwritable maybe we shouldn't bail and still add an
    // entry to the TLB

    struct tlb_entry *tlb = &mem->tlb[TLB_INDEX(addr)];
    tlb->page = TLB_PAGE(addr);
    if (P_WRITABLE(pt->flags))
        tlb->page_if_writable = tlb->page;
    else
        // 1 is not a valid page so this won't look like a hit
        tlb->page_if_writable = TLB_PAGE_EMPTY;
    tlb->data_minus_addr = (uintptr_t) pt->data - TLB_PAGE(addr);
    mem->dirty_page = TLB_PAGE(addr);
    return (void *) (tlb->data_minus_addr + addr);
}

__attribute__((constructor))
static void check_pagesize(void) {
    if (sysconf(_SC_PAGESIZE) != 1 << 12) {
        fprintf(stderr, "wtf is this page size\n");
        abort();
    }
}
