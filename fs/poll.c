#include "kernel/task.h"
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include "misc.h"
#include "util/list.h"
#include "kernel/errno.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "fs/poll.h"

// lock order: fd, then poll

struct poll *poll_create() {
    struct poll *poll = malloc(sizeof(struct poll));
    if (poll == NULL)
        return NULL;
    poll->waiters = 0;
    poll->notify_pipe[0] = -1;
    poll->notify_pipe[1] = -1;
    list_init(&poll->poll_fds);
    lock_init(&poll->lock);
    return poll;
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

    poll_fd->types = types;
    poll_fd->info = info;

    err = 0;
out:
    unlock(&poll->lock);
    unlock(&fd->poll_lock);
    return err;
}

void poll_wake(struct fd *fd) {
    struct poll_fd *poll_fd;
    lock(&fd->poll_lock);
    list_for_each_entry(&fd->poll_fds, poll_fd, polls) {
        struct poll *poll = poll_fd->poll;
        lock(&poll->lock);
        if (poll->notify_pipe[1] != -1)
            write(poll->notify_pipe[1], "", 1);
        unlock(&poll->lock);
    }
    unlock(&fd->poll_lock);
}

int poll_wait(struct poll *poll_, poll_callback_t callback, void *context, struct timespec *timeout) {
    lock(&poll_->lock);
    if (poll_->waiters++ == 0) {
        assert(poll_->notify_pipe[0] == -1 && poll_->notify_pipe[1] == -1);
        if (pipe(poll_->notify_pipe) < 0) {
            unlock(&poll_->lock);
            return errno_map();
        }
        fcntl(poll_->notify_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(poll_->notify_pipe[1], F_SETFL, O_NONBLOCK);
    }

    // TODO this is pretty broken with regards to timeouts
    int res = 0;
    while (true) {
        // check if any fds are ready
        struct poll_fd *poll_fd;
        list_for_each_entry(&poll_->poll_fds, poll_fd, fds) {
            struct fd *fd = poll_fd->fd;
            int poll_types;
            if (fd->ops->poll) {
                poll_types = fd->ops->poll(fd) & poll_fd->types;
            } else {
                struct pollfd p = {.fd = fd->real_fd, .events = poll_fd->types};
                if (poll(&p, 1, 0) > 0)
                    poll_types = p.revents;
                else
                    poll_types = 0;
            }
            if (poll_types) {
                // POLLNVAL should only be returned by poll() when given a bad fd
                assert(!(poll_types & POLL_NVAL));
                if (callback(context, fd, poll_types, poll_fd->info) == 1)
                    res++;
            }
        }
        if (res > 0)
            break;

        // wait for a ready notification
        size_t pollfd_count = 1;
        list_for_each_entry(&poll_->poll_fds, poll_fd, fds) {
            if (poll_fd->fd->ops->poll == NULL)
                pollfd_count++;
        }
        struct pollfd pollfds[pollfd_count];
        pollfds[0].fd = poll_->notify_pipe[0];
        pollfds[0].events = POLLIN;
        int i = 1;
        list_for_each_entry(&poll_->poll_fds, poll_fd, fds) {
            if (poll_fd->fd->ops->poll)
                continue;
            pollfds[i].fd = poll_fd->fd->real_fd;
            // TODO translate flags
            pollfds[i].events = poll_fd->types;
            i++;
        }

        unlock(&poll_->lock);
        int timeout_millis = -1;
        if (timeout != NULL)
            timeout_millis = timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000;
        int err = poll(pollfds, pollfd_count, timeout_millis);
        lock(&poll_->lock);
        if (err < 0) {
            res = errno_map();
            break;
        }
        if (err == 0)
            // timed out and still nobody is ready
            break;

        if (pollfds[0].revents & POLLIN) {
            char fuck;
            if (read(poll_->notify_pipe[0], &fuck, 1) != 1) {
                res = errno_map();
                break;
            }
        }
    }

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

    free(poll);
}
