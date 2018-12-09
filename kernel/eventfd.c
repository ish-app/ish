#include "kernel/calls.h"
#include "kernel/fs.h"
#include "fs/poll.h"

static struct fd_ops eventfd_ops;

#define EFD_CLOEXEC_ 0x80000
#define EFD_NONBLOCK_ 0x800

int_t sys_eventfd2(uint_t initval, int_t flags) {
    STRACE("eventfd(%d, %#x)", initval, flags);
    if (flags & ~(EFD_CLOEXEC_|EFD_NONBLOCK_))
        return _EINVAL;

    struct fd *fd = adhoc_fd_create();
    if (fd == NULL)
        return _ENOMEM;
    fd->ops = &eventfd_ops;

    cond_init(&fd->eventfd_cond);
    fd->eventfd_val = initval;

    fd_t f = f_install(fd);
    if (f >= 0) {
        if (flags & EFD_CLOEXEC_)
            f_set_cloexec(f);
        if (flags & EFD_NONBLOCK_)
            fd->flags |= O_NONBLOCK_;
    }
    return f;
}
int_t sys_eventfd(uint_t initval) {
    return sys_eventfd2(initval, 0);
}

static ssize_t eventfd_read(struct fd *fd, void *buf, size_t bufsize) {
    if (bufsize < sizeof(uint64_t))
        return _EINVAL;

    lock(&fd->lock);
    while (fd->eventfd_val == 0) {
        if (fd->flags & O_NONBLOCK_) {
            unlock(&fd->lock);
            return _EAGAIN;
        }
        wait_for(&fd->eventfd_cond, &fd->lock, NULL);
    }

    *(uint64_t *) buf = fd->eventfd_val;
    fd->eventfd_val = 0;
    notify(&fd->eventfd_cond);
    unlock(&fd->lock);
    return sizeof(uint64_t);
}

static ssize_t eventfd_write(struct fd *fd, const void *buf, size_t bufsize) {
    if (bufsize < sizeof(uint64_t))
        return _EINVAL;
    uint64_t increment = *(uint64_t *) buf;
    if (increment == UINT64_MAX)
        return _EINVAL;

    lock(&fd->lock);
    while (fd->eventfd_val >= UINT64_MAX - increment) {
        if (fd->flags & O_NONBLOCK_) {
            unlock(&fd->lock);
            return _EAGAIN;
        }
        wait_for(&fd->eventfd_cond, &fd->lock, NULL);
    }

    fd->eventfd_val += increment;
    notify(&fd->eventfd_cond);
    unlock(&fd->lock);
    return sizeof(uint64_t);
}

static int eventfd_poll(struct fd *fd) {
    lock(&fd->lock);
    int types = 0;
    if (fd->eventfd_val > 0)
        types |= POLL_READ;
    if (fd->eventfd_val < UINT64_MAX - 1)
        types |= POLL_WRITE;
    unlock(&fd->lock);
    return types;
}

static struct fd_ops eventfd_ops = {
    .read = eventfd_read,
    .write = eventfd_write,
    .poll = eventfd_poll,
};
