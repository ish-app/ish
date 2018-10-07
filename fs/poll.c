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
    list_init(&poll->poll_fds);
    list_init(&poll->real_poll_fds);
    lock_init(&poll->lock);
    return poll;
}

int poll_add_fd(struct poll *poll, struct fd *fd, int types) {
    struct poll_fd *poll_fd = malloc(sizeof(struct poll_fd));
    if (poll_fd == NULL)
        return _ENOMEM;
    poll_fd->fd = fd;
    poll_fd->poll = poll;
    poll_fd->types = types;

    lock(&fd->lock);
    lock(&poll->lock);
    list_add(&fd->poll_fds, &poll_fd->polls);
    if (fd->ops->poll) {
        list_add(&poll->poll_fds, &poll_fd->fds);
    } else {
        list_add(&poll->real_poll_fds, &poll_fd->fds);
    }
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

int poll_wait(struct poll *poll_, struct poll_event *event, int timeout) {
    int res;
    // TODO this is pretty broken with regards to timeouts
    lock(&poll_->lock);
    while (true) {
        // check if any fds are ready
        struct poll_fd *poll_fd;
        list_for_each_entry(&poll_->poll_fds, poll_fd, fds) {
            struct fd *fd = poll_fd->fd;
            if (fd->ops->poll(fd) & poll_fd->types) {
                event->fd = fd;
                event->types = poll_fd->types;
                unlock(&poll_->lock);
                return 1;
            }
        }

        // wait for a ready notification
        if (pipe(poll_->notify_pipe) < 0) {
            res = errno_map();
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

        unlock(&poll_->lock);
        res = poll(pollfds, pollfd_count, timeout);
        lock(&poll_->lock);
        if (res < 0) {
            res = errno_map();
            break;
        }
        if (res == 0)
            break;

        if (pollfds[0].revents & POLLIN) {
            char fuck;
            if (read(poll_->notify_pipe[0], &fuck, 1) != 1) {
                res = errno_map();
                break;
            }
        } else {
            i = 1;
            // FIXME real_poll_fds could change since pollfds array was created,
            // this is probably a race condition
            // epoll will reflect modifications to the list immediately
            list_for_each_entry(&poll_->real_poll_fds, poll_fd, fds) {
                if (pollfds[i].revents) {
                    event->fd = poll_fd->fd;
                    // TODO translate flags
                    event->types = pollfds[i].revents;
                    goto finished_poll;
                }
                i++;
            }
        }

        close(poll_->notify_pipe[0]);
        poll_->notify_pipe[0] = -1;
        close(poll_->notify_pipe[1]);
        poll_->notify_pipe[1] = -1;
    }

finished_poll:
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

    free(poll);
}
