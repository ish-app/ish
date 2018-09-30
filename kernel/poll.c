#include "debug.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "fs/poll.h"
#include "kernel/calls.h"

static int user_read_or_zero(addr_t addr, void *data, size_t size) {
    if (addr == 0)
        memset(data, 0, size);
    else if (user_read(addr, data, size))
        return _EFAULT;
    return 0;
}

dword_t sys_select(fd_t nfds, addr_t readfds_addr, addr_t writefds_addr, addr_t exceptfds_addr, addr_t timeout_addr) {
    STRACE("select(%d, 0x%x, 0x%x, 0x%x, 0x%x)", nfds, readfds_addr, writefds_addr, exceptfds_addr, timeout_addr);
    size_t fdset_size = BITS_SIZE(nfds);
    char readfds[fdset_size];
    if (user_read_or_zero(readfds_addr, readfds, fdset_size))
        return _EFAULT;
    char writefds[fdset_size];
    if (user_read_or_zero(writefds_addr, writefds, fdset_size))
        return _EFAULT;
    char exceptfds[fdset_size];
    if (user_read_or_zero(exceptfds_addr, exceptfds, fdset_size))
        return _EFAULT;

    int timeout = -1;
    if (timeout_addr != 0) {
        struct timeval_ timeout_timeval;
        if (user_get(timeout_addr, timeout))
            return _EFAULT;
        timeout = timeout_timeval.usec / 1000 + timeout_timeval.sec * 1000;
    }

    // current implementation only works with one fd
    fd_t fd = -1;
    int types = 0;
    for (fd_t i = 0; i < nfds; i++) {
        if (bit_test(i, readfds) || bit_test(i, writefds) || bit_test(i, exceptfds)) {
            if (fd != -1)
                TODO("select with multiple fds");
            fd = i;
            if (bit_test(i, readfds))
                types |= POLL_READ;
            if (bit_test(i, writefds))
                types |= POLL_WRITE;
            /* if (bit_test(i, exceptfds)) */
            /*     FIXME("poll exceptfds"); */
        }
    }

    struct poll *poll = poll_create();
    if (poll == NULL)
        return _ENOMEM;
    poll_add_fd(poll, f_get(fd), types);
    struct poll_event event;
    int err = poll_wait(poll, &event, timeout);
    if (err < 0) {
        poll_destroy(poll);
        return err;
    }

    memset(readfds, 0, fdset_size);
    memset(writefds, 0, fdset_size);
    memset(exceptfds, 0, fdset_size);
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

    return err;
}

dword_t sys_poll(addr_t fds, dword_t nfds, dword_t timeout) {
    if (nfds != 1)
        TODO("actual working poll");

    STRACE("poll(0x%x, %d, %d)", fds, nfds, timeout);
    struct pollfd_ fake_poll;
    if (user_get(fds, fake_poll))
        return _EFAULT;
    struct poll *poll = poll_create();
    if (poll == NULL)
        return _ENOMEM;
    poll_add_fd(poll, f_get(fake_poll.fd), fake_poll.events);
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
