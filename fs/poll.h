#ifndef FS_POLL_H
#define FS_POLL_H
#include "kernel/fs.h"

struct poll {
    struct list poll_fds;
    int notify_pipe[2];
    int waiters;
    lock_t lock;
};

struct poll_fd {
    // locked by containing struct poll
    struct fd *fd;
    struct list fds;
    int types;
    union poll_fd_info {
        void *ptr;
        int fd;
        uint64_t num;
    } info;

    // locked by containing struct fd
    struct poll *poll;
    struct list polls;
};

// these are defined in system headers somewhere
#undef POLL_PRI
#undef POLL_ERR
#undef POLL_HUP

#define POLL_READ 1
#define POLL_PRI 2
#define POLL_WRITE 4
#define POLL_ERR 8
#define POLL_HUP 16
#define POLL_NVAL 32
struct poll_event {
    struct fd *fd;
    int types;
};
struct poll *poll_create(void);
bool poll_has_fd(struct poll *poll, struct fd *fd);
int poll_add_fd(struct poll *poll, struct fd *fd, int types, union poll_fd_info info);
int poll_mod_fd(struct poll *poll, struct fd *fd, int types, union poll_fd_info info);
int poll_del_fd(struct poll *poll, struct fd *fd);
// please do not call this while holding any locks you would acquire in your poll operation
void poll_wakeup(struct fd *fd);
// Waits for events on the fds in this poll, and calls the callback for each one found.
// Returns the number of times the callback returned 1, or negative for error.
typedef int (*poll_callback_t)(void *context, int types, union poll_fd_info info);
int poll_wait(struct poll *poll, poll_callback_t callback, void *context, struct timespec *timeout);
// does not lock the poll because lock ordering, you must ensure no other
// thread will add or remove fds from this poll
void poll_destroy(struct poll *poll);

#endif
