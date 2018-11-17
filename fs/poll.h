#ifndef FS_POLL_H
#define FS_POLL_H
#include "kernel/fs.h"

struct poll {
    struct list poll_fds;
    int notify_pipe[2];
    lock_t lock;
};

struct poll_fd {
    // locked by containing struct poll
    struct fd *fd;
    struct list fds;
    int types;

    // locked by containing struct fd
    struct poll *poll;
    struct list polls;
};

#define POLL_READ 1
#define POLL_WRITE 4
struct poll_event {
    struct fd *fd;
    int types;
};
struct poll *poll_create(void);
int poll_add_fd(struct poll *poll, struct fd *fd, int types);
int poll_del_fd(struct poll *poll, struct fd *fd);
// please do not call this while holding any locks you would acquire in your poll operation
void poll_wake(struct fd *fd);
int poll_wait(struct poll *poll, struct poll_event *event, int timeout);
// does not lock the poll because lock ordering, you must ensure no other
// thread will add or remove fds from this poll
void poll_destroy(struct poll *poll);

#endif
