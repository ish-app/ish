#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "sys/errno.h"
#include "emu/memory.h"

// this code currently assumes the system page size is 4k

pagetable pt_alloc(void) {
    return calloc(PT_SIZE, sizeof(struct pt_entry *));
}

int pt_map(pagetable pt, page_t start, pages_t pages, page *memory, unsigned flags) {
    if (memory == MAP_FAILED) {
        return err_map(errno);
    }
    for (page_t page = start; page < start + pages; page++) {
        if (pt[page] != NULL) {
            pt_unmap(pt, page, 1);
        }
        struct pt_entry *entry = malloc(sizeof(struct pt_entry));
        if (entry == NULL) return _ENOMEM;
        entry->data = memory;
        entry->refcount = 1;
        entry->flags = flags;
        pt[page] = entry;
        memory++;
    }
    return 0;
}

void pt_unmap(pagetable pt, page_t start, pages_t pages) {
    for (page_t page = start; page < start + pages; page++) {
        struct pt_entry *entry = pt[page];
        pt[page] = NULL;
        entry->refcount--;
        if (entry->refcount == 0) {
            free(entry);
        }
    }
}

int pt_map_nothing(pagetable pt, page_t start, pages_t pages, unsigned flags) {
    page *memory = mmap(NULL, pages * sizeof(page),
            (flags & P_WRITABLE ? PROT_WRITE : 0) | PROT_READ,
            MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    return pt_map(pt, start, pages, memory, flags);
}


int pt_map_file(pagetable pt, page_t start, pages_t pages, int fd, off_t off, unsigned flags) {
    void *memory = mmap(NULL, pages * sizeof(page),
            (flags & P_WRITABLE ? PROT_WRITE : 0) | PROT_READ,
            MAP_PRIVATE, fd, off);
    return pt_map(pt, start, pages, memory, flags);
}

void pt_dump(pagetable pt) {
    for (unsigned i = 0; i < PT_SIZE; i++) {
        if (pt[i] != NULL) {
            TRACE("page     %u", i);
            TRACE("data at  %p", pt[i]->data);
            TRACE("refcount %u", pt[i]->refcount);
            TRACE("flags    %x", pt[i]->flags);
        }
    }
}

__attribute__((constructor))
static void check_pagesize(void) {
    if (sysconf(_SC_PAGESIZE) != 1 << 12) {
        fprintf(stderr, "wtf is this page size\n");
        abort();
    }
}
