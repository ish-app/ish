#include "kernel/calls.h"
#include "kernel/fs.h"
#include "fs/poll.h"
#include "kernel/errno.h"
#include "kernel/atomic.h"

// Constants for eventfd
#define EFD_SEMAPHORE (1 << 0)     // Provides semaphore-like behavior
#define EFD_CLOEXEC   O_CLOEXEC_   // Set close-on-exec flag
#define EFD_NONBLOCK  O_NONBLOCK_  // Set non-blocking mode
#define EFD_FLAGS_MASK (EFD_SEMAPHORE | EFD_CLOEXEC | EFD_NONBLOCK)

// Structure to hold eventfd-specific data
struct eventfd_context {
    atomic_uint64_t counter;    // Atomic counter for thread safety
    unsigned int flags;         // Flags controlling behavior
    struct wait_queue wait;     // Wait queue for blocking operations
};

static struct fd_ops eventfd_ops;

/**
 * Creates a new eventfd object.
 * @param initval Initial value for the counter
 * @param flags Configuration flags
 * @return File descriptor on success, negative errno on failure
 */
int_t sys_eventfd2(uint_t initval, int_t flags) {
    STRACE("eventfd2(%u, %#x)", initval, flags);

    // Validate flags
    if (flags & ~EFD_FLAGS_MASK)
        return _EINVAL;
    
    // Check initial value
    if (initval > UINT64_MAX)
        return _EINVAL;

    // Allocate and initialize fd
    struct fd *fd = adhoc_fd_create(&eventfd_ops);
    if (fd == NULL)
        return _ENOMEM;

    // Initialize eventfd context
    struct eventfd_context *ctx = kmalloc(sizeof(struct eventfd_context));
    if (ctx == NULL) {
        fd_close(fd);
        return _ENOMEM;
    }

    atomic_store(&ctx->counter, initval);
    ctx->flags = flags;
    wait_queue_init(&ctx->wait);
    fd->eventfd.context = ctx;

    return f_install(fd, flags & (EFD_CLOEXEC | EFD_NONBLOCK));
}

/**
 * Legacy interface for eventfd creation.
 */
int_t sys_eventfd(uint_t initval) {
    return sys_eventfd2(initval, 0);
}

/**
 * Reads current counter value.
 * In semaphore mode, reads 1 if counter > 0.
 */
static ssize_t eventfd_read(struct fd *fd, void *buf, size_t bufsize) {
    struct eventfd_context *ctx = fd->eventfd.context;
    uint64_t oldval, newval;

    if (bufsize < sizeof(uint64_t))
        return _EINVAL;

    while (1) {
        oldval = atomic_load(&ctx->counter);
        
        if (oldval > 0) {
            if (ctx->flags & EFD_SEMAPHORE)
                newval = oldval - 1;
            else
                newval = 0;
                
            if (atomic_compare_exchange_strong(&ctx->counter, &oldval, newval)) {
                *(uint64_t *)buf = (ctx->flags & EFD_SEMAPHORE) ? 1 : oldval;
                wake_up(&ctx->wait);
                poll_wakeup(fd, POLL_WRITE);
                return sizeof(uint64_t);
            }
            continue;
        }

        if (fd->flags & O_NONBLOCK_)
            return _EAGAIN;

        int ret = wait_event_interruptible(&ctx->wait,
            atomic_load(&ctx->counter) > 0);
        if (ret)
            return _EINTR;
    }
}

/**
 * Increments counter by the specified value.
 */
static ssize_t eventfd_write(struct fd *fd, const void *buf, size_t bufsize) {
    struct eventfd_context *ctx = fd->eventfd.context;
    uint64_t increment;
    uint64_t oldval, newval;

    if (bufsize < sizeof(uint64_t))
        return _EINVAL;

    increment = *(const uint64_t *)buf;
    if (increment == UINT64_MAX)
        return _EINVAL;

    while (1) {
        oldval = atomic_load(&ctx->counter);
        if (UINT64_MAX - increment < oldval) {
            if (fd->flags & O_NONBLOCK_)
                return _EAGAIN;

            int ret = wait_event_interruptible(&ctx->wait,
                atomic_load(&ctx->counter) < UINT64_MAX - increment);
            if (ret)
                return _EINTR;
            continue;
        }

        newval = oldval + increment;
        if (atomic_compare_exchange_strong(&ctx->counter, &oldval, newval)) {
            wake_up(&ctx->wait);
            poll_wakeup(fd, POLL_READ);
            return sizeof(uint64_t);
        }
    }
}

/**
 * Polls the eventfd for read/write availability.
 */
static int eventfd_poll(struct fd *fd) {
    struct eventfd_context *ctx = fd->eventfd.context;
    uint64_t val = atomic_load(&ctx->counter);
    int types = 0;

    if (val > 0)
        types |= POLL_READ;
    if (val < UINT64_MAX - 1)
        types |= POLL_WRITE;

    return types;
}

/**
 * Cleans up resources when eventfd is closed.
 */
static int eventfd_close(struct fd *fd) {
    struct eventfd_context *ctx = fd->eventfd.context;
    wake_up_all(&ctx->wait);
    kfree(ctx);
    return 0;
}

static struct fd_ops eventfd_ops = {
    .read = eventfd_read,
    .write = eventfd_write,
    .poll = eventfd_poll,
    .close = eventfd_close,
};
