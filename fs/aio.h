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

enum aioctx_op : uint16_t {
    AIOCTX_PREAD = 0,
    AIOCTX_PWRITE = 1,
    AIOCTX_FSYNC = 2,
    AIOCTX_FDSYNC = 3,
    AIOCTX_POLL = 5,
    AIOCTX_NOOP = 6,
    AIOCTX_PREADV = 7,
    AIOCTX_PWRITEV = 8,
};

// A pending I/O event's information.
struct aioctx_event_pending {
    // The operation to perform.
    enum aioctx_op op;

    // The open file to perform it on.
    fd_t fd;

    // A guest memory buffer to read to or write from.
    uint64_t buf;

    // The bounds of the guest memory buffer.
    uint64_t nbytes;

    // The file offset to perform the I/O operation.
    int64_t offset;
};

// A completed I/O event's information.
struct aioctx_event_complete {
    // Result values for the event.
    int64_t result[2];
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
        struct aioctx_event_pending as_pending;

        // Tag: AIOCTX_COMPLETE
        struct aioctx_event_complete as_complete;
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

// Retrieve a pointer to a given AIO context by it's ID.
// 
// This returns NULL if no such context exists. Otherwise, it will
// automatically retain the context before unlocking it's owning table.
struct aioctx *aioctx_table_get_and_retain(struct aioctx_table *tbl, unsigned int ctx_id);

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

// Submit a pending I/O event to the AIO context.
// 
// This returns a positive integer corresponding to the event index within the
// context. This index remains stable and can be used to access the pending
// event data up until the event is resolved.
signed int aioctx_submit_pending_event(struct aioctx *ctx, uint64_t user_data, struct aioctx_event_pending pending_data);

// Cancel a pending I/O event, freeing the event index for reuse.
// 
// This should only be used if the submitted FD has signalled a synchronous
// error (e.g. EINVAL) which indicates that it does not plan to complete the
// event later.
void aioctx_cancel_event(struct aioctx *ctx, unsigned int index);

void aioctx_lock(struct aioctx* ctx);
void aioctx_unlock(struct aioctx* ctx);

// Get a pending event from the AIOCTX.
// 
// The event structure will be returned by writing it's pointer to the **event
// parameter.
// 
// This function returns _EINVAL if the given index is not a valid event, not a
// pending event, or if the context has been released by it's supporting task.
// In the event that this function returns an error, the event should be
// considered cancelled. Any resources related to the event should be disposed
// of.
// 
// This function is not synchronized and returns pointers to the context's
// internal structures. As such, you must retain and lock the table before
// calling this function, and drop all internal pointers before unlocking or
// releasing the context. Do not hold the lock for longer than necessary as you
// may serialize or deadlock other I/O requests.
signed int aioctx_get_pending_event(struct aioctx *ctx, unsigned int index, struct aioctx_event_pending **event);

#endif