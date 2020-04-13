#include "kernel/task.h"
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <limits.h>
#include "misc.h"
#include "util/list.h"
#include "kernel/errno.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "fs/poll.h"
#include "fs/real.h"

#include "fs/sockrestart.h"

#if defined(__linux__)
#include <sys/epoll.h>
#define HAVE_EPOLL 1
#elif defined(__APPLE__)
#include <sys/event.h>
#define HAVE_KQUEUE 1
#endif

int real_poll_init(struct real_poll *real);
void real_poll_close(struct real_poll *real);
struct real_poll_event {
#if HAVE_EPOLL
    struct epoll_event real;
#elif HAVE_KQUEUE
    struct kevent real;
#endif
};
int real_poll_wait(struct real_poll *real, struct real_poll_event *events, int max, struct timespec *timeout);
int real_poll_update(struct real_poll *real, int fd, int types);

// lock order: fd, then poll

struct poll *poll_create() {
    struct poll *poll = malloc(sizeof(struct poll));
    if (poll == NULL)
        return ERR_PTR(_ENOMEM);
    int err = real_poll_init(&poll->real);
    if (err < 0)
        return ERR_PTR(errno_map());
    poll->waiters = 0;
    poll->notify_pipe[0] = -1;
    poll->notify_pipe[1] = -1;
    list_init(&poll->poll_fds);
    lock_init(&poll->lock);
    return poll;
}

static inline bool poll_fd_is_real(struct poll_fd *pollfd) {
    return pollfd->fd->ops->poll == realfs_poll;
}

// does not do its own locking
static struct poll_fd *poll_find_fd(struct poll *poll, struct fd *fd) {
    struct poll_fd *poll_fd, *tmp;
    list_for_each_entry_safe(&poll->poll_fds, poll_fd, tmp, fds) {
        if (poll_fd->fd == fd)
            return poll_fd;
    }
    return NULL;
}

bool poll_has_fd(struct poll *poll, struct fd *fd) {
    return poll_find_fd(poll, fd) != NULL;
}

int poll_add_fd(struct poll *poll, struct fd *fd, int types, union poll_fd_info info) {
    int err;
    lock(&fd->poll_lock);
    lock(&poll->lock);

    struct poll_fd *poll_fd = malloc(sizeof(struct poll_fd));
    if (poll_fd == NULL) {
        err = _ENOMEM;
        goto out;
    }
    poll_fd->fd = fd;
    poll_fd->poll = poll;
    poll_fd->types = types;
    poll_fd->info = info;

    if (poll_fd_is_real(poll_fd)) {
        err = real_poll_update(&poll->real, fd->real_fd, types);
        if (err < 0) {
            free(poll_fd);
            err = errno_map();
            goto out;
        }
    }

    list_add(&fd->poll_fds, &poll_fd->polls);
    list_add(&poll->poll_fds, &poll_fd->fds);

    err = 0;
out:
    unlock(&poll->lock);
    unlock(&fd->poll_lock);
    return err;
}

int poll_del_fd(struct poll *poll, struct fd *fd) {
    int err;
    lock(&fd->poll_lock);
    lock(&poll->lock);
    struct poll_fd *poll_fd = poll_find_fd(poll, fd);
    if (poll_fd == NULL) {
        err = _ENOENT;
        goto out;
    }

    if (poll_fd_is_real(poll_fd)) {
        err = real_poll_update(&poll->real, fd->real_fd, 0);
        if (err < 0) {
            err = errno_map();
            goto out;
        }
    }

    list_remove(&poll_fd->polls);
    list_remove(&poll_fd->fds);
    free(poll_fd);

    err = 0;
out:
    unlock(&poll->lock);
    unlock(&fd->poll_lock);
    return err;
}

int poll_mod_fd(struct poll *poll, struct fd *fd, int types, union poll_fd_info info) {
    int err;
    lock(&fd->poll_lock);
    lock(&poll->lock);
    struct poll_fd *poll_fd = poll_find_fd(poll, fd);
    if (poll_fd == NULL) {
        err = _ENOENT;
        goto out;
    }

    if (poll_fd_is_real(poll_fd)) {
        err = real_poll_update(&poll->real, fd->real_fd, types);
        if (err < 0) {
            err = errno_map();
            goto out;
        }
    }

    poll_fd->types = types;
    poll_fd->info = info;

    err = 0;
out:
    unlock(&poll->lock);
    unlock(&fd->poll_lock);
    return err;
}

void poll_wakeup(struct fd *fd) {
    struct poll_fd *poll_fd;
    lock(&fd->poll_lock);
    list_for_each_entry(&fd->poll_fds, poll_fd, polls) {
        struct poll *poll = poll_fd->poll;
        lock(&poll->lock);
        if (poll->notify_pipe[1] != -1)
            write(poll->notify_pipe[1], "", 1);
        unlock(&poll->lock);
        // oneshot?
    }
    unlock(&fd->poll_lock);
}

int poll_wait(struct poll *poll_, poll_callback_t callback, void *context, struct timespec *timeout) {
    lock(&poll_->lock);

    // acquire the pipe
    if (poll_->waiters++ == 0) {
        assert(poll_->notify_pipe[0] == -1 && poll_->notify_pipe[1] == -1);
        if (pipe(poll_->notify_pipe) < 0) {
            unlock(&poll_->lock);
            return errno_map();
        }
        fcntl(poll_->notify_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(poll_->notify_pipe[1], F_SETFL, O_NONBLOCK);
        real_poll_update(&poll_->real, poll_->notify_pipe[0], POLL_READ);
    }

    // TODO this is pretty broken with regards to timeouts
    int res = 0;
    while (true) {
        // check if any fds are ready
        struct poll_fd *poll_fd, *tmp;
        list_for_each_entry_safe(&poll_->poll_fds, poll_fd, tmp, fds) {
            struct fd *fd = poll_fd->fd;
            int poll_types = 0;
            if (fd->ops->poll)
                poll_types = fd->ops->poll(fd);
            poll_types &= poll_fd->types | POLL_HUP | POLL_ERR;
            if (poll_types) {
                if (callback(context, poll_types, poll_fd->info) == 1)
                    res++;

                // The real poll does not actually get the FDs set as oneshot.
                // But this loop is done while holding the lock, so only one
                // thread can get each oneshot event. This doesn't solve the
                // thundering herd problem at all, but at least the semantics
                // are right. I'll just leave that as a TODO.
                if (poll_fd->types & POLL_ONESHOT) {
                    list_remove(&poll_fd->polls);
                    list_remove(&poll_fd->fds);
                    if (poll_fd_is_real(poll_fd)) {
                        real_poll_update(&poll_->real, fd->real_fd, 0);
                    }
                    free(poll_fd);
                }
            }
        }
        if (res > 0)
            break;

        // wait for a ready notification
        list_for_each_entry(&poll_->poll_fds, poll_fd, fds) {
            sockrestart_begin_listen_wait(poll_fd->fd);
        }
        unlock(&poll_->lock);
        int err;
        do {
            struct real_poll_event e;
            err = real_poll_wait(&poll_->real, &e, 1, timeout);
        } while (sockrestart_should_restart_listen_wait() && errno == EINTR);
        lock(&poll_->lock);
        list_for_each_entry(&poll_->poll_fds, poll_fd, fds) {
            sockrestart_end_listen_wait(poll_fd->fd);
        }

        if (err < 0) {
            res = errno_map();
            break;
        }
        if (err == 0) {
            // timed out and still nobody is ready
            break;
        }

        char fuck;
        if (read(poll_->notify_pipe[0], &fuck, 1) < 0 && errno != EAGAIN) {
            res = errno_map();
            break;
        }
    }

    // release the pipe
    if (--poll_->waiters == 0) {
        close(poll_->notify_pipe[0]);
        close(poll_->notify_pipe[1]);
        poll_->notify_pipe[0] = -1;
        poll_->notify_pipe[1] = -1;
    }

    unlock(&poll_->lock);
    return res;
}

void poll_destroy(struct poll *poll) {
    struct poll_fd *poll_fd;
    struct poll_fd *tmp;
    list_for_each_entry_safe(&poll->poll_fds, poll_fd, tmp, fds) {
        lock(&poll_fd->fd->poll_lock);
        list_remove(&poll_fd->polls);
        list_remove(&poll_fd->fds);
        unlock(&poll_fd->fd->poll_lock);
        free(poll_fd);
    }

    real_poll_close(&poll->real);
    free(poll);
}

// Platform-specific real_poll implementations

#if HAVE_EPOLL

int real_poll_init(struct real_poll *real) {
    real->fd = epoll_create1(0);
    if (real->fd < 0)
        return -1;
    return 0;
}

int real_poll_wait(struct real_poll *real, struct real_poll_event *events, int max, struct timespec *timeout) {
    int timeout_millis = -1;
    if (timeout != NULL)
        timeout_millis = timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000;
    return epoll_wait(real->fd, (struct epoll_event *) events, max, timeout_millis);
}

int real_poll_update(struct real_poll *real, int fd, int types) {
    types &= ~EPOLLONESHOT;
    if (types == 0)
        return epoll_ctl(real->fd, EPOLL_CTL_DEL, fd, NULL);
    struct epoll_event epevent = {.events = types};
    int err = epoll_ctl(real->fd, EPOLL_CTL_MOD, fd, &epevent);
    if (err < 0 && errno == ENOENT)
        err = epoll_ctl(real->fd, EPOLL_CTL_ADD, fd, &epevent);
    return err;
}

#elif HAVE_KQUEUE

int real_poll_init(struct real_poll *real) {
    real->fd = kqueue();
    if (real->fd < 0)
        return -1;
    return 0;
}

int real_poll_update(struct real_poll *real, int fd, int types) {
    struct kevent e[3]; // one for EVFILT_READ, EVFILT_WRITE, EVFILT_EXCEPT
    if (types & POLL_READ) {
        EV_SET(&e[0], fd, EVFILT_READ, EV_ADD, 0, 0, 0);
    } else if (types & POLL_HUP) {
        // Set the low water mark really high so we'll only get woken up on a hangup
        EV_SET(&e[0], fd, EVFILT_READ, EV_ADD, NOTE_LOWAT, INT_MAX, 0);
    } else {
        EV_SET(&e[0], fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
    }
    EV_SET(&e[1], fd, EVFILT_WRITE, types & POLL_WRITE ? EV_ADD : EV_DELETE, 0, 0, 0);
    EV_SET(&e[2], fd, EVFILT_EXCEPT, types & POLL_ERR ? EV_ADD : EV_DELETE, 0, 0, 0);
    for (int i = 0; i < 3; i++) {
        e[i].flags |= EV_RECEIPT;
    }

    return kevent(real->fd, e, 3, e, 3, NULL);
}

int real_poll_wait(struct real_poll *real, struct real_poll_event *events, int max, struct timespec *timeout) {
    return kevent(real->fd, NULL, 0, (struct kevent *) events, max, timeout);
}

#endif

void real_poll_close(struct real_poll *real) {
    close(real->fd);
}

