#include <poll.h>
#include "misc.h"
#include "util/list.h"
#include "sys/errno.h"
#include "sys/fs.h"

struct poll *poll_create() {
    struct poll *poll = malloc(sizeof(struct poll));
    if (poll == NULL)
        return NULL;
    list_init(&poll->poll_fds);
    list_init(&poll->real_poll_fds);
    lock_init(&poll->lock);
    return poll;
}

int poll_add_fd(struct poll *poll, struct fd *fd, int types) {
    struct poll_fd *poll_fd = malloc(sizeof(struct poll_fd));
    if (fd == NULL)
        return _ENOMEM;

    poll_fd->fd = fd;
    poll_fd->poll = poll;
    poll_fd->types = types;

    lock(poll);
    if (fd->ops->poll) {
        list_add(&poll->poll_fds, &poll_fd->fds);
    } else {
        list_add(&poll->real_poll_fds, &poll_fd->fds);
    }
    unlock(poll);
    lock(fd);
    list_add(&fd->poll_fds, &poll_fd->polls);
    unlock(fd);

    return 0;
}

int poll_del_fd(struct poll *poll, struct fd *fd) {
    lock(poll);
    struct poll_fd *poll_fd;
    struct poll_fd *tmp;
    list_for_each_entry_safe(&poll->poll_fds, poll_fd, tmp, fds) {
        if (poll_fd->fd == fd) {
            lock(fd);
            list_remove(&poll_fd->polls);
            unlock(fd);
            list_remove(&poll_fd->fds);
            free(poll_fd);
            unlock(poll);
            return 0;
        }
    }
    return _EINVAL;
}

void poll_wake_pollable(struct pollable *pollable) {
    lock(pollable);
    struct fd *fd;
    list_for_each_entry(&pollable->fds, fd, pollable_other_fds) {
        struct poll_fd *poll_fd;
        lock(fd);
        list_for_each_entry(&fd->poll_fds, poll_fd, polls) {
            struct poll *poll = poll_fd->poll;
            if (poll->notify_pipe[1] != -1)
                write(poll->notify_pipe[1], "", 1);
        }
        unlock(fd);
    }
    unlock(pollable);
}

int poll_wait(struct poll *poll_, struct poll_event *event, int timeout) {
    int res;
    // TODO this is pretty broken with regards to timeouts
    lock(poll_);
    while (true) {
        // check if any fds are ready
        struct poll_fd *poll_fd;
        list_for_each_entry(&poll_->poll_fds, poll_fd, fds) {
            struct fd *fd = poll_fd->fd;
            if (fd->ops->poll(fd) & poll_fd->types) {
                event->fd = fd;
                event->types = poll_fd->types;
                unlock(poll_);
                return 1;
            }
        }

        // wait for a ready notification
        if (pipe(poll_->notify_pipe) < 0) {
            res = err_map(errno);
            break;
        }
        size_t pollfd_count = list_size(&poll_->real_poll_fds) + 1;
        struct pollfd pollfds[pollfd_count];
        pollfds[0].fd = poll_->notify_pipe[0];
        pollfds[0].events = POLLIN;

        int i = 1;
        list_for_each_entry(&poll_->real_poll_fds, poll_fd, fds) {
            pollfds[i].fd = poll_fd->fd->real_fd;
            // TODO translate flags
            pollfds[i].events = poll_fd->types;
            i++;
        }

        unlock(poll_);
        res = poll(pollfds, pollfd_count, timeout);
        lock(poll_);
        if (res < 0) {
            res = err_map(errno);
            break;
        }
        if (res == 0)
            break;

        if (pollfds[0].revents & POLLIN) {
            char fuck;
            if (read(poll_->notify_pipe[0], &fuck, 1) != 1) {
                res = err_map(errno);
                break;
            }
        } else {
            i = 1;
            // FIXME real_poll_fds could change since pollfds array was created,
            // this is probably a race condition
            list_for_each_entry(&poll_->real_poll_fds, poll_fd, fds) {
                if (pollfds[i].revents) {
                    event->fd = poll_fd->fd;
                    // TODO translate flags
                    event->types = pollfds[i].revents;
                    unlock(poll_);
                    return 1;
                }
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
    unlock(poll_);
    return res;
}

void poll_destroy(struct poll *poll) {
    lock(poll);

    struct poll_fd *poll_fd;
    struct poll_fd *tmp;
    list_for_each_entry_safe(&poll->poll_fds, poll_fd, tmp, fds) {
        lock(poll_fd->fd);
        list_remove(&poll_fd->polls);
        unlock(poll_fd->fd);
        list_remove(&poll_fd->fds);
        free(poll_fd);
    }

    free(poll);
}
