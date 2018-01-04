#include "debug.h"
#include "kernel/fs.h"
#include "kernel/calls.h"

static int bit(int index, void *mem) {
    char *c = mem;
    return c[index >> 3] & (1 << (index & 7)) ? 1 : 0;
}

static void bit_set(int index, void *mem) {
    char *c = mem;
    c[index >> 3] |= 1 << (index & 7);
}

static int user_read_or_zero(addr_t addr, void *data, size_t size) {
    if (addr == 0)
        memset(data, size, 0);
    else if (user_read(addr, data, size))
        return _EFAULT;
    return 0;
}

dword_t sys_select(fd_t nfds, addr_t readfds_addr, addr_t writefds_addr, addr_t exceptfds_addr, addr_t timeout_addr) {
    size_t fdset_size = ((nfds - 1) / 8) + 1;
    char readfds[fdset_size];
    if (user_read_or_zero(readfds_addr, readfds, fdset_size))
        return _EFAULT;
    char writefds[fdset_size];
    if (user_read_or_zero(writefds_addr, writefds, fdset_size))
        return _EFAULT;
    char exceptfds[fdset_size];
    if (user_read_or_zero(exceptfds_addr, exceptfds, fdset_size))
        return _EFAULT;

    // current implementation only works with one fd
    fd_t fd = -1;
    int types = 0;
    for (fd_t i = 0; i < nfds; i++) {
        if (bit(i, readfds) || bit(i, writefds) || bit(i, exceptfds)) {
            if (fd != -1)
                TODO("select with multiple fds");
            fd = i;
            if (bit(i, readfds))
                types |= POLL_READ;
            if (bit(i, writefds))
                types |= POLL_WRITE;
            if (bit(i, exceptfds))
                TODO("poll exceptfds");
        }
    }

    struct poll *poll = poll_create();
    if (poll == NULL)
        return _ENOMEM;
    poll_add_fd(poll, current->files[fd], types);
    struct poll_event event;
    int err = poll_wait(poll, &event, -1);
    if (err < 0) {
        poll_destroy(poll);
        return err;
    }

    memset(readfds, fdset_size, 0);
    memset(writefds, fdset_size, 0);
    memset(exceptfds, fdset_size, 0);
    if (event.types & POLL_READ)
        bit_set(fd, readfds);
    if (event.types & POLL_WRITE)
        bit_set(fd, writefds);
    if (readfds_addr && user_write(readfds_addr, readfds, fdset_size))
        return _EFAULT;
    if (writefds_addr && user_write(writefds_addr, writefds, fdset_size))
        return _EFAULT;
    if (exceptfds_addr && user_write(exceptfds_addr, exceptfds, fdset_size))
        return _EFAULT;

    return 0;
}

dword_t sys_poll(addr_t fds, dword_t nfds, dword_t timeout) {
    if (nfds != 1)
        TODO("actual working poll");

    struct pollfd_ fake_poll;
    if (user_get(fds, fake_poll))
        return _EFAULT;
    struct poll *poll = poll_create();
    if (poll == NULL)
        return _ENOMEM;
    poll_add_fd(poll, current->files[fake_poll.fd], fake_poll.events);
    struct poll_event event;
    int err = poll_wait(poll, &event, timeout);
    if (err < 0) {
        poll_destroy(poll);
        return err;
    }
    fake_poll.revents = event.types;
    poll_destroy(poll);
    if (user_put(fds, fake_poll))
        return _EFAULT;
    return err;
}
