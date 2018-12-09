#include <poll.h>
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
    poll->notify_pipe[0] = -1;
    poll->notify_pipe[1] = -1;
    list_init(&poll->poll_fds);
    lock_init(&poll->lock);
    return poll;
}

int poll_add_fd(struct poll *poll, struct fd *fd, int types, union poll_fd_info info) {
    struct poll_fd *poll_fd = malloc(sizeof(struct poll_fd));
    if (poll_fd == NULL)
        return _ENOMEM;
    poll_fd->fd = fd;
    poll_fd->poll = poll;
    poll_fd->types = types;
    poll_fd->info = info;

    lock(&fd->lock);
    lock(&poll->lock);
    list_add(&fd->poll_fds, &poll_fd->polls);
    list_add(&poll->poll_fds, &poll_fd->fds);
    unlock(&poll->lock);
    unlock(&fd->lock);

    return 0;
}

int poll_del_fd(struct poll *poll, struct fd *fd) {
    struct poll_fd *poll_fd, *tmp;
    int ret = _EINVAL;

    lock(&fd->lock);
    lock(&poll->lock);
    list_for_each_entry_safe(&poll->poll_fds, poll_fd, tmp, fds) {
        if (poll_fd->fd == fd) {
            list_remove(&poll_fd->polls);
            list_remove(&poll_fd->fds);
            free(poll_fd);
            ret = 0;
            break;
        }
    }
    unlock(&poll->lock);
    unlock(&fd->lock);

    return ret;
}

void poll_wake(struct fd *fd) {
    struct poll_fd *poll_fd;
    lock(&fd->lock);
    list_for_each_entry(&fd->poll_fds, poll_fd, polls) {
        struct poll *poll = poll_fd->poll;
        lock(&poll->lock);
        if (poll->notify_pipe[1] != -1)
            write(poll->notify_pipe[1], "", 1);
        unlock(&poll->lock);
    }
    unlock(&fd->lock);
}

int poll_wait(struct poll *poll_, poll_callback_t callback, void *context, int timeout) {
    int res = 0;
    // TODO this is pretty broken with regards to timeouts
    lock(&poll_->lock);
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
                if (callback(context, fd, poll_types, poll_fd->info) == 1)
                    res++;
            }
        }
        if (res > 0)
            break;

        // wait for a ready notification
        if (pipe(poll_->notify_pipe) < 0) {
            res = errno_map();
            break;
        }
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
        int err = poll(pollfds, pollfd_count, timeout);
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

        close(poll_->notify_pipe[0]);
        poll_->notify_pipe[0] = -1;
        close(poll_->notify_pipe[1]);
        poll_->notify_pipe[1] = -1;
    }

    if (poll_->notify_pipe[0] != -1)
        close(poll_->notify_pipe[0]);
    if (poll_->notify_pipe[1] != -1)
        close(poll_->notify_pipe[1]);
    unlock(&poll_->lock);
    return res;
}

void poll_destroy(struct poll *poll) {
    struct poll_fd *poll_fd;
    struct poll_fd *tmp;
    list_for_each_entry_safe(&poll->poll_fds, poll_fd, tmp, fds) {
        lock(&poll_fd->fd->lock);
        list_remove(&poll_fd->polls);
        unlock(&poll_fd->fd->lock);
        list_remove(&poll_fd->fds);
        free(poll_fd);
    }
    list_for_each_entry_safe(&poll->poll_fds, poll_fd, tmp, fds) {
        lock(&poll_fd->fd->lock);
        list_remove(&poll_fd->polls);
        unlock(&poll_fd->fd->lock);
        list_remove(&poll_fd->fds);
        free(poll_fd);
    }

    free(poll);
}
