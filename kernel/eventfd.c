#include "kernel/calls.h"
#include "kernel/fs.h"
#include "fs/poll.h"

static struct fd_ops eventfd_ops;

int_t sys_eventfd2(uint_t initval, int_t flags) {
    STRACE("eventfd(%d, %#x)", initval, flags);
    if (flags & ~(O_CLOEXEC_|O_NONBLOCK_))
        return _EINVAL;

    struct fd *fd = adhoc_fd_create(&eventfd_ops);
    if (fd == NULL)
        return _ENOMEM;
    fd->eventfd.val = initval;
    return f_install(fd, flags);
}
int_t sys_eventfd(uint_t initval) {
    return sys_eventfd2(initval, 0);
}

static ssize_t eventfd_read(struct fd *fd, void *buf, size_t bufsize) {
    if (bufsize < sizeof(uint64_t))
        return _EINVAL;

    lock(&fd->lock);
    while (fd->eventfd.val == 0) {
        if (fd->flags & O_NONBLOCK_) {
            unlock(&fd->lock);
            return _EAGAIN;
        }
        if (wait_for(&fd->cond, &fd->lock, NULL)) {
            unlock(&fd->lock);
            return _EINTR;
        }
    }

    *(uint64_t *) buf = fd->eventfd.val;
    fd->eventfd.val = 0;
    notify(&fd->cond);
    unlock(&fd->lock);
    poll_wakeup(fd, POLL_WRITE);
    return sizeof(uint64_t);
}

static ssize_t eventfd_write(struct fd *fd, const void *buf, size_t bufsize) {
    if (bufsize < sizeof(uint64_t))
        return _EINVAL;
    uint64_t increment = *(uint64_t *) buf;
    if (increment == UINT64_MAX)
        return _EINVAL;

    lock(&fd->lock);
    while (fd->eventfd.val >= UINT64_MAX - increment) {
        if (fd->flags & O_NONBLOCK_) {
            unlock(&fd->lock);
            return _EAGAIN;
        }
        if (wait_for(&fd->cond, &fd->lock, NULL)) {
            unlock(&fd->lock);
            return _EINTR;
        }
    }

    fd->eventfd.val += increment;
    notify(&fd->cond);
    unlock(&fd->lock);
    poll_wakeup(fd, POLL_READ);
    return sizeof(uint64_t);
}

static int eventfd_poll(struct fd *fd) {
    lock(&fd->lock);
    int types = 0;
    if (fd->eventfd.val > 0)
        types |= POLL_READ;
    if (fd->eventfd.val < UINT64_MAX - 1)
        types |= POLL_WRITE;
    unlock(&fd->lock);
    return types;
}

static struct fd_ops eventfd_ops = {
    .read = eventfd_read,
    .write = eventfd_write,
    .poll = eventfd_poll,
};
