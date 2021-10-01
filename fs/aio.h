#ifndef FS_AIO_H
#define FS_AIO_H

#include "util/sync.h"
#include "fs/fd.h"
#include "misc.h"

typedef dword_t aio_context_t;

enum aioctx_event_tag {
    // The event slot is empty and should be initialized before being used.
    // 
    // Conveniently, this matches the zero value, so initializing the entire
    // event will also make it None.
    AIOCTX_NONE = 0,

    // The event slot is occupied with a pending I/O request.
    // 
    // This corresponds to the as_pending variant of the data union.
    AIOCTX_PENDING = 1,

    // The event slot is occupied with a completed I/O request.
    // 
    // This corresponds to the as_complete variant of the data union.
    AIOCTX_COMPLETE = 2,
};

enum aioctx_op {
    AIOCTX_PREAD = 0,
    AIOCTX_PWRITE = 1,
    AIOCTX_FSYNC = 2,
    AIOCTX_FDSYNC = 3,
    AIOCTX_POLL = 5,
    AIOCTX_NOOP = 6,
    AIOCTX_PREADV = 7,
    AIOCTX_PWRITEV = 8,
};

// A single AIO completion event.
// 
// This structure is nullable, aioctx_event->tag == AIOCTX_NONE means that the
// rest of the fields have yet to be initialized.
struct aioctx_event {
    enum aioctx_event_tag tag;

    // A data value provided by the user to identify in-flight requests from
    // the kernel.
    uint64_t user_data;

    union {
        // Tag: AIOCTX_PENDING
        struct {
            // The operation to perform.
            enum aioctx_op op;

            // The open file to perform it on.
            fd_t fd;

            // A guest memory buffer to read to or write from.
            addr_t buf;

            // The bounds of the guest memory buffer.
            size_t size;

            // The current guest memory buffer offset.
            ssize_t offset;
        } as_pending;

        // Tag: AIOCTX_COMPLETE
        struct {
            // Result values for the event.
            int64_t result[2];
        } as_complete;
    } data;
};

// An AIO context.
// 
// Individual AIO contexts are refcounted and locked independently from the
// tables that hold them.
struct aioctx {
    atomic_uint refcount;
    lock_t lock;
    
    // Indicates if this context is owned by a task.
    // 
    // If true, then the `pid` field is guaranteed to be valid, and correspond
    // to the task that made the request. If false, then the `pid` is invalid,
    // and any pending or completed events should be treated as cancelled.
    bool is_owned_by_task;

    // The process that currently owns the context.
    pid_t pid;

    // The capacity of the events structure.
    // 
    // This is specified by `io_setup`; requests that would potentially
    // overflow the events table should be rejected with `_EAGAIN`.
    dword_t events_capacity;

    // The current table of pending and completed events.
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

// Insert an AIO context into a given table.
// 
// The return value will be a positive index into the context table if the
// context was successfully inserted, or an error code otherwise.
// 
// The context must be non-null. There is no provision for inserting a null
// context into the table.
signed int aioctx_table_insert(struct aioctx_table *tbl, struct aioctx *ctx);

// Remove an AIO context from the table by it's position (context ID).
// 
// This returns an error code if the context ID is not valid for this table.
// 
// All pending I/O requests on the given context will retain the context until
// they resolve. The context will also be flagged as having been released by
// the task, which is treated as an implicit cancellation of any pending
// requests.
signed int aioctx_table_remove(struct aioctx_table *tbl, unsigned int ctx_id);

struct aioctx *aioctx_new(int events_capacity, pid_t pid);
void aioctx_retain(struct aioctx *ctx);
void aioctx_release(struct aioctx *ctx);

// Release the AIO context and flag it as no longer being owned by a valid
// task.
// 
// All pending I/O requests on the given context will retain the context until
// they resolve. The context will also be flagged as having been released by
// the task, which is treated as an implicit cancellation of any pending
// requests.
void aioctx_release_from_task(struct aioctx *ctx);

#endif