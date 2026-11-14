// pidfd_open: a file descriptor that stands for a process and becomes readable
// (for poll, select and epoll) when that process has terminated.
#include "kernel/calls.h"
#include "kernel/task.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "fs/poll.h"
#include "fs/proc.h"

extern const struct fd_ops procfs_fdops;

static struct fd_ops pidfd_ops;
static lock_t pidfds_lock = LOCK_INITIALIZER;
static struct list pidfds = {&pidfds, &pidfds};

int_t sys_pidfd_open(pid_t_ pid, uint_t flags) {
    STRACE("pidfd_open(%d, %#x)", pid, flags);
    if (pid <= 0 || (flags & ~O_NONBLOCK_))
        return _EINVAL;

    lock(&pids_lock);
    struct task *task = pid_get_task_zombie(pid);
    if (task == NULL) {
        unlock(&pids_lock);
        return _ESRCH;
    }
    if (task->group->leader != task) {
        // a thread, not a process
        unlock(&pids_lock);
        return _EINVAL;
    }
    struct fd *fd = adhoc_fd_create(&pidfd_ops);
    if (fd == NULL) {
        unlock(&pids_lock);
        return _ENOMEM;
    }
    fd->pidfd.pid = pid;
    fd->pidfd.dead = task->zombie;
    lock(&pidfds_lock);
    list_add(&pidfds, &fd->pidfd.others);
    unlock(&pidfds_lock);
    unlock(&pids_lock);
    // a pidfd is always close-on-exec
    return f_install(fd, flags | O_CLOEXEC_);
}

// Called when the last thread of a process has exited, without pids_lock.
void pidfd_notify_exit(pid_t_ pid) {
    lock(&pidfds_lock);
    struct fd *fd;
    list_for_each_entry(&pidfds, fd, pidfd.others) {
        if (fd->pidfd.pid == pid && !fd->pidfd.dead) {
            fd->pidfd.dead = true;
            poll_wakeup(fd, POLL_READ);
        }
    }
    unlock(&pidfds_lock);
}

// The process a pidfd stands for, or an error. *dead says whether it has exited.
pid_t_ pidfd_pid(fd_t f, bool *dead) {
    struct fd *fd = f_get(f);
    if (fd == NULL)
        return _EBADF;
    if (fd->ops == &procfs_fdops && fd->proc.entry.meta == &proc_pid) {
        // an open /proc/<pid> directory is the older kind of pidfd
        lock(&pids_lock);
        bool alive = pid_get_task(fd->proc.entry.pid) != NULL;
        unlock(&pids_lock);
        if (dead != NULL)
            *dead = !alive;
        return fd->proc.entry.pid;
    }
    if (fd->ops != &pidfd_ops)
        return _EBADF;
    if (dead != NULL)
        *dead = fd->pidfd.dead;
    return fd->pidfd.pid;
}

int_t sys_pidfd_send_signal(fd_t f, int_t sig, addr_t info_addr, uint_t flags) {
    STRACE("pidfd_send_signal(%d, %d, %#x, %#x)", f, sig, info_addr, flags);
    if (flags != 0)
        return _EINVAL;
    bool dead;
    pid_t_ pid = pidfd_pid(f, &dead);
    if (pid < 0)
        return pid;
    // (the siginfo a caller may pass is not forwarded; the signal is sent as kill does)
    if (dead)
        return _ESRCH;
    return sys_kill(pid, sig);
}

// no lock: called with poll locks held, and the flag only ever goes to true
static int pidfd_poll(struct fd *fd) {
    return fd->pidfd.dead ? POLL_READ : 0;
}

static int pidfd_close(struct fd *fd) {
    lock(&pidfds_lock);
    list_remove(&fd->pidfd.others);
    unlock(&pidfds_lock);
    return 0;
}

static struct fd_ops pidfd_ops = {
    .poll = pidfd_poll,
    .close = pidfd_close,
};
