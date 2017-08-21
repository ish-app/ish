#include <poll.h>
#include "sys/errno.h"
#include "sys/fs.h"

struct poll *poll_create() {
    struct poll *poll = malloc(sizeof(struct poll));
    if (poll == NULL)
        return NULL;
    list_init(&poll->poll_fds);
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
    list_add(&poll->poll_fds, &poll_fd->fds);
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
            if (poll->notify_pipe[1] != -1) {
                write(poll->notify_pipe[1], &poll_fd, sizeof(poll_fd));
            }
        }
        unlock(fd);
    }
    unlock(pollable);
}

int poll_wait(struct poll *poll_, struct poll_event *event, int timeout) {
    // TODO this is pretty broken with regards to timeouts

    lock(poll_);
    // make a pipe so things can notify us that things happened
    if (pipe(poll_->notify_pipe) < 0)
        return err_map(errno);
    int pipe_rd = poll_->notify_pipe[0];

    // wait for events on the notify pipe
    struct pollfd pollfd;
retry:
    pollfd.fd = pipe_rd;
    pollfd.events = POLLIN;
    unlock(poll_);
    int err = poll(&pollfd, 1, timeout);
    // TODO it's possible the poll_fd struct got freed while that poll call was
    // happening, so check for that or something
    lock(poll_);
    if (err < 0) {
        err = err_map(errno);
        goto done;
    }
    if (err == 0)
        goto done;

    struct poll_fd *poll_fd;
    if (read(pipe_rd, &poll_fd, sizeof(poll_fd)) != sizeof(poll_fd)) {
        err = err_map(errno);
        goto done;
    }

    if (!(poll_fd->fd->ops->poll(poll_fd->fd) & poll_fd->types))
        goto retry;

    event->fd = poll_fd->fd;
    event->types = poll_fd->types;
done:
    close(poll_->notify_pipe[0]);
    close(poll_->notify_pipe[1]);
    unlock(poll_);
    return err;
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
