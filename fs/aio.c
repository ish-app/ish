#include "fs/aio.h"
#include "kernel/errno.h"
#include <limits.h>
#include <string.h>

// Ensure a minimum capacity in the AIOCTX table.
// 
// AIOCTX must be locked before resizing the table. The lock can be elided in
// contexts where you know the table is not shared yet.
// 
// Attempts to shrink the table will be rejected silently.
// May return _ENOMEM if memory for the new table could not be allocated.
static int _aioctx_table_ensure(struct aioctx_table *tbl, unsigned int newcap) {
    if (tbl == NULL) return 0;
    if (tbl->capacity >= newcap) return 0;
    if ((INT_MAX / sizeof(struct aioctx*)) < newcap) return _ENOMEM;

    struct aioctx **new_contexts = malloc(sizeof(struct aioctx*) * newcap);
    if (new_contexts == NULL) return _ENOMEM;

    memset(new_contexts, 0, sizeof(struct aioctx*) * newcap);
    if (tbl->contexts) {
        memcpy(new_contexts, tbl->contexts, sizeof(struct aioctx*) * tbl->capacity);
        free(tbl->contexts);
    }

    tbl->contexts = new_contexts;
    tbl->capacity = newcap;

    return 0;
}

struct aioctx *aioctx_new(int events_capacity, pid_t pid) {
    if ((INT_MAX / sizeof(struct aioctx_event)) < events_capacity) return NULL;

    struct aioctx *aioctx = malloc(sizeof(struct aioctx));
    if (aioctx == NULL) return NULL;

    struct aioctx_event *aioctx_events = malloc(sizeof(struct aioctx_event) * events_capacity);
    if (aioctx_events == NULL) {
        free(aioctx);
        return NULL;
    }

    memset(aioctx_events, 0, sizeof(struct aioctx_event) * events_capacity);

    lock_init(&aioctx->lock);

    aioctx->refcount = 1;
    aioctx->events_capacity = events_capacity;
    aioctx->events = aioctx_events;
    aioctx->is_owned_by_task = true;
    aioctx->pid = pid;

    return aioctx;
}

void aioctx_retain(struct aioctx *ctx) {
    if (ctx == NULL) return;

    lock(&ctx->lock);
    ctx->refcount++;
    unlock(&ctx->lock);
}

static void _aioctx_decrement_ref(struct aioctx *ctx) {
    if (--ctx->refcount == 0) {
        free(ctx->events);
        free(ctx);
    } else {
        unlock(&ctx->lock);
    }
}

void aioctx_release(struct aioctx *ctx) {
    if (ctx == NULL) return;

    lock(&ctx->lock);
    _aioctx_decrement_ref(ctx);
}

void aioctx_release_from_task(struct aioctx *ctx) {
    if (ctx == NULL) return;

    lock(&ctx->lock);
    ctx->is_owned_by_task = false;
    _aioctx_decrement_ref(ctx);
}

signed int aioctx_submit_pending_event(struct aioctx *ctx, uint64_t user_data, addr_t iocbp, struct aioctx_event_pending pending_data) {
    if (ctx == NULL) return _EINVAL;

    lock(&ctx->lock);

    signed int index = _EAGAIN;

    for (int i = 0; i < ctx->events_capacity; i += 1) {
        if (ctx->events[i].tag == AIOCTX_NONE) {
            index = i;
            
            ctx->events[i].tag = AIOCTX_PENDING;
            ctx->events[i].user_data = user_data;
            ctx->events[i].iocb_obj = iocbp;
            ctx->events[i].data.as_pending = pending_data;

            break;
        }
    }

    unlock(&ctx->lock);

    return index;
}

void aioctx_cancel_event(struct aioctx *ctx, unsigned int index) {
    if (ctx == NULL) return;
    
    lock(&ctx->lock);

    if (index >= ctx->events_capacity) return;

    if (ctx->events[index].tag == AIOCTX_PENDING)
        ctx->events[index].tag = AIOCTX_NONE;

    unlock(&ctx->lock);
}

void aioctx_complete_event(struct aioctx *ctx, unsigned int index, int64_t result0, int64_t result1) {
    if (ctx == NULL) return;

    lock(&ctx->lock);

    if (index >= ctx->events_capacity) return;

    if (ctx->events[index].tag == AIOCTX_PENDING) {
        ctx->events[index].tag = AIOCTX_COMPLETE;

        struct aioctx_event_complete data;

        data.result[0] = result0;
        data.result[1] = result1;

        ctx->events[index].data.as_complete = data;
    }

    unlock(&ctx->lock);
}

bool aioctx_consume_completed_event(struct aioctx *ctx, uint64_t *user_data, addr_t *iocbp, struct aioctx_event_complete *completed_data) {
    if (ctx == NULL) return false;

    bool result = false;

    lock(&ctx->lock);

    for (int i = 0; i < ctx->events_capacity; i += 1) {
        if (ctx->events[i].tag == AIOCTX_COMPLETE) {
            *user_data = ctx->events[i].user_data;
            *iocbp = ctx->events[i].iocb_obj;
            *completed_data = ctx->events[i].data.as_complete;

            ctx->events[i].tag = AIOCTX_NONE;
            result = true;

            break;
        }
    }

    unlock(&ctx->lock);

    return result;
}

void aioctx_lock(struct aioctx* ctx) {
    if (ctx == NULL) return;

    lock(&ctx->lock);
}

void aioctx_unlock(struct aioctx* ctx) {
    if (ctx == NULL) return;

    unlock(&ctx->lock);
}

signed int aioctx_get_pending_event(struct aioctx *ctx, unsigned int index, struct aioctx_event_pending **event) {
    if (ctx == NULL) return _EINVAL;
    if (!ctx->is_owned_by_task) return _EINVAL;
    if (index >= ctx->events_capacity) return _EINVAL;
    if (ctx->events[index].tag != AIOCTX_PENDING) return _EINVAL;

    if (event != NULL) *event = &ctx->events[index].data.as_pending;

    return 0;
}

struct aioctx_table *aioctx_table_new(unsigned int capacity) {
    struct aioctx_table *tbl = malloc(sizeof(struct aioctx_table));
    if (tbl == NULL) return NULL;
    
    tbl->capacity = 0;
    tbl->contexts = NULL;
    lock_init(&tbl->lock);

    int err = _aioctx_table_ensure(tbl, capacity);
    if (err < 0) return ERR_PTR(err);

    return tbl;
}

void aioctx_table_delete(struct aioctx_table *tbl) {
    if (tbl == NULL) return;
    
    lock(&tbl->lock);
    for (int i = 0; i < tbl->capacity; i += 1) {
        if (tbl->contexts[i] != NULL) {
            aioctx_release_from_task(tbl->contexts[i]);
        }
    }
    free(tbl->contexts);
    free(tbl);
}

signed int aioctx_table_insert(struct aioctx_table *tbl, struct aioctx *ctx) {
    if (tbl == NULL) return _EINVAL;
    if (ctx == NULL) return _EINVAL;

    lock(&tbl->lock);
    
    for (int i = 0; i < tbl->capacity; i += 1) {
        if (tbl->contexts[i] == NULL) {
            tbl->contexts[i] = ctx;
            aioctx_retain(ctx);
            unlock(&tbl->lock);
            return i;
        }
    }

    //At this point, we've scanned the entire table and every entry is full.
    int old_capacity = tbl->capacity;
    if (((INT_MAX - 1) / 2) <= old_capacity) return _ENOMEM;

    int err = _aioctx_table_ensure(tbl, (tbl->capacity * 2) + 1);
    if (err < 0) return err;

    tbl->contexts[old_capacity] = ctx;

    aioctx_retain(ctx);
    unlock(&tbl->lock);

    return old_capacity;
}

signed int aioctx_table_remove(struct aioctx_table *tbl, unsigned int ctx_id) {
    if (tbl == NULL) return _EINVAL;
    
    lock(&tbl->lock);

    if (ctx_id >= tbl->capacity) {
        unlock(&tbl->lock);
        return _EINVAL;
    }

    struct aioctx *ctx = tbl->contexts[ctx_id];
    if (ctx == NULL) {
        unlock(&tbl->lock);
        return _EINVAL;
    }

    aioctx_release_from_task(ctx);
    tbl->contexts[ctx_id] = NULL;

    unlock(&tbl->lock);

    return 0;
}

struct aioctx *aioctx_table_get_and_retain(struct aioctx_table *tbl, unsigned int ctx_id) {
    if (tbl == NULL) return NULL;

    lock(&tbl->lock);

    if (ctx_id >= tbl->capacity) {
        unlock(&tbl->lock);
        return NULL;
    }

    struct aioctx *ctx = tbl->contexts[ctx_id];
    if (ctx != NULL) {
        aioctx_retain(ctx);
    }

    unlock(&tbl->lock);

    return ctx;
}