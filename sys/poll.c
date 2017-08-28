#define DEFAULT_CHANNEL debug
#include "debug.h"
#include "sys/fs.h"
#include "sys/calls.h"

dword_t sys_poll(addr_t fds, dword_t nfds, dword_t timeout) {
    if (nfds != 1)
        TODO("actual working poll");

    struct pollfd_ fake_poll;
    if (user_get(fds, fake_poll))
        return _EFAULT;
    TRACELN("polling");
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
    TRACELN("poll done, returning 1");
    return err;
}
