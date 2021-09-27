#ifndef FS_AIO_H
#define FS_AIO_H

#include "util/sync.h"
#include "misc.h"

typedef dword_t aio_context_t;

// A single AIO completion event.
// 
// This structure is nullable, aioctx_event->in_use == FALSE means that the
// rest of the fields have yet to be initialized.
struct aioctx_event {
    bool in_use;

    // A data value provided by the user to identify in-flight requests from
    // the kernel.
    uint64_t user_data;

    // Result values for the event.
    int64_t result[2];
};

// An AIO context.
// 
// Individual AIO contexts are refcounted and locked independently from the
// tables that hold them.
struct aioctx {
    atomic_uint refcount;
    lock_t lock;

    // The capacity of the events structure.
    // 
    // This is specified by `io_setup`; requests that would potentially
    // overflow the events table should be rejected with `_EAGAIN`.
    dword_t events_capacity;

    // The current table of pending events.
    struct aioctx_event* events;
};

// The table of AIO contexts for a given process.
// 
// The context table may be locked, but it is not refcounted and cannot be
// shared across tasks.
struct aioctx_table {
    lock_t lock;

    // The capacity of the contexts table.
    unsigned capacity;

    // Storage for the contexts table.
    // 
    // This is an array-of-pointers to allow efficient reallocation. Individual
    // entries are nullable.
    struct aioctx **contexts;
};

struct aioctx_table *aioctx_table_new(unsigned int capacity);
void aioctx_table_delete(struct aioctx_table *tbl);

struct aioctx *aioctx_new(int events_capacity);
void aioctx_retain(struct aioctx *ctx);
void aioctx_release(struct aioctx *ctx);

#endif