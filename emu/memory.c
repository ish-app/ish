#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "sys/errno.h"
#include "emu/memory.h"

// this code currently assumes the system page size is 4k

void mem_init(struct mem *mem) {
    mem->pt = calloc(PT_SIZE, sizeof(struct pt_entry *));
    mem->tlb = calloc(TLB_SIZE, sizeof(struct tlb_entry));
    for (unsigned i = 0; i < TLB_SIZE; i++) {
        mem->tlb[i].page = mem->tlb[i].page_if_writable = TLB_PAGE_EMPTY;
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
            pt_unmap(mem, page, 1);
        }
        struct pt_entry *entry = malloc(sizeof(struct pt_entry));
        if (entry == NULL) return _ENOMEM;
        entry->data = memory;
        entry->refcount = 1;
        entry->flags = flags;
        mem->pt[page] = entry;
        memory = (char *) memory + PAGE_SIZE;
    }
    if (flags & P_GROWSDOWN) {
        pt_map(mem, start - 1, 1, NULL, P_GUARD);
    }
    return 0;
}

void pt_unmap(struct mem *mem, page_t start, pages_t pages) {
    for (page_t page = start; page < start + pages; page++) {
        struct pt_entry *entry = mem->pt[page];
        mem->pt[page] = NULL;
        entry->refcount--;
        if (entry->refcount == 0) {
            // TODO actually free the memory
            free(entry);
        }
    }
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

void pt_dump(struct mem *mem) {
    for (unsigned i = 0; i < PT_SIZE; i++) {
        if (mem->pt[i] != NULL) {
            TRACE("page     %u", i);
            TRACE("data at  %p", mem->pt[i]->data);
            TRACE("refcount %u", mem->pt[i]->refcount);
            TRACE("flags    %x", mem->pt[i]->flags);
        }
    }
}

void *tlb_handle_miss(struct mem *mem, addr_t addr, int type) {
    struct pt_entry *pt = mem->pt[PAGE(addr)];
    if (pt == NULL)
        return NULL; // page does not exist
    if (type == TLB_WRITE && !(pt->flags & P_WRITABLE))
        return NULL; // unwritable

    // TODO if page is unwritable maybe we shouldn't bail and still add an
    // entry to the TLB

    struct tlb_entry *tlb = &mem->tlb[TLB_INDEX(addr)];
    tlb->page = TLB_PAGE(addr);
    if (pt->flags & P_WRITABLE)
        tlb->page_if_writable = tlb->page;
    else
        // 1 is not a valid page so this won't look like a hit
        tlb->page_if_writable = TLB_PAGE_EMPTY;
    tlb->data = pt->data;
    mem->dirty_page = PAGE(addr);
    return (char *) tlb->data + OFFSET(addr);
}

__attribute__((constructor))
static void check_pagesize(void) {
    if (sysconf(_SC_PAGESIZE) != 1 << 12) {
        fprintf(stderr, "wtf is this page size\n");
        abort();
    }
}
