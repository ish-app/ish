#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "kernel/calls.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "fs/path.h"
#include "fs/poll.h"
#include "fs/stat.h"
#include "fs/inode.h"
#include "fs/inotify.h"

struct watch {
    struct list list;
    int wd;
    uint32_t mask;
    // what is watched (the mount is only compared, never used)
    struct mount *mount;
    uint64_t ino;
};

struct event {
    struct list list;
    int wd;
    uint32_t mask;
    uint32_t cookie;
    uint32_t len; // of name with padding, 0 for no name
    char name[];
};

struct inotify {
    struct list instances;
    struct fd *fd;
    struct list watches;
    struct list events;
    unsigned nevents;
    int next_wd;
    // read by poll without the lock
    atomic_uint queued;
};

#define MAX_QUEUED_EVENTS 16384

// one lock for all instances, their watches and their queues
static lock_t inotify_lock = LOCK_INITIALIZER;
static struct list instances = {&instances, &instances};
atomic_uint inotify_watches;
static atomic_uint next_cookie;

static struct fd_ops inotify_ops;

// must hold inotify_lock
static void queue_event(struct inotify *ino, int wd, uint32_t mask, uint32_t cookie, const char *name) {
    size_t name_len = name ? strlen(name) : 0;
    // the name is followed by at least one zero and padded to a multiple of 16
    uint32_t len = name_len ? (uint32_t) ((name_len + 16) & ~(size_t) 15) : 0;

    if (!list_empty(&ino->events)) {
        // an event equal to the newest one in the queue is not queued again
        struct event *last = list_entry(ino->events.prev, struct event, list);
        if (last->wd == wd && last->mask == mask && last->cookie == cookie && last->len == len &&
                (len == 0 || strcmp(last->name, name) == 0))
            return;
    }
    if (ino->nevents >= MAX_QUEUED_EVENTS) {
        struct event *last = list_entry(ino->events.prev, struct event, list);
        if (last->mask == IN_Q_OVERFLOW_)
            return;
        wd = -1;
        mask = IN_Q_OVERFLOW_;
        cookie = 0;
        len = 0;
    }
    struct event *ev = calloc(1, sizeof(struct event) + len);
    if (ev == NULL)
        return;
    ev->wd = wd;
    ev->mask = mask;
    ev->cookie = cookie;
    ev->len = len;
    if (len)
        memcpy(ev->name, name, name_len);
    list_add_tail(&ino->events, &ev->list);
    ino->nevents++;
    ino->queued = ino->nevents;
    notify(&ino->fd->cond);
    poll_wakeup(ino->fd, POLL_READ);
}

// must hold inotify_lock
static void watch_remove(struct inotify *ino, struct watch *watch) {
    queue_event(ino, watch->wd, IN_IGNORED_, 0, NULL);
    list_remove(&watch->list);
    free(watch);
    inotify_watches--;
}

void inotify_subject(struct mount *mount, const char *path, struct inotify_subject *subject) {
    memset(subject, 0, sizeof(*subject));
    subject->mount = mount;
    struct statbuf stat;
    if (mount->fs->stat(mount, path, &stat) == 0) {
        subject->self = stat.inode;
        subject->nlink = stat.nlink;
        subject->dir = S_ISDIR(stat.mode);
    }
    // the directory that holds it, and its name there
    const char *slash = strrchr(path, '/');
    if (slash == NULL || slash[1] == '\0' || strlen(slash + 1) >= sizeof(subject->name))
        return;
    char parent[MAX_PATH];
    size_t parent_len = (size_t) (slash - path);
    memcpy(parent, path, parent_len);
    parent[parent_len] = '\0';
    if (mount->fs->stat(mount, parent, &stat) == 0) {
        subject->parent = stat.inode;
        strcpy(subject->name, slash + 1);
    }
}

void inotify_event(const struct inotify_subject *subject, uint32_t self_mask, uint32_t child_mask, uint32_t cookie) {
    if (!inotify_active())
        return;
    uint32_t dir = subject->dir ? IN_ISDIR_ : 0;
    lock(&inotify_lock);
    struct inotify *ino;
    for (int self = 0; self <= 1; self++) {
        uint64_t number = self ? subject->self : subject->parent;
        uint32_t mask = self ? self_mask : child_mask;
        if (number == 0 || mask == 0)
            continue;
        list_for_each_entry(&instances, ino, instances) {
            struct watch *watch, *tmp;
            list_for_each_entry_safe(&ino->watches, watch, tmp, list) {
                if (watch->ino != number || watch->mount != subject->mount)
                    continue;
                uint32_t events = mask & watch->mask & IN_ALL_EVENTS_;
                if (events == 0)
                    continue;
                // (Linux does not mark these two as being about a directory)
                uint32_t isdir = (events & (IN_DELETE_SELF_ | IN_MOVE_SELF_)) ? 0 : dir;
                queue_event(ino, watch->wd, events | isdir, cookie, self ? NULL : subject->name);
                if ((events & IN_DELETE_SELF_) || (watch->mask & IN_ONESHOT_))
                    watch_remove(ino, watch);
            }
        }
    }
    unlock(&inotify_lock);
}

void inotify_notify_fd(struct fd *fd, uint32_t mask) {
    if (!inotify_active() || fd->mount == NULL || fd->inode == NULL)
        return;
    if (!S_ISREG(fd->type) && !S_ISDIR(fd->type))
        return;
    struct inotify_subject subject = {.mount = fd->mount, .self = fd->inode->number, .dir = S_ISDIR(fd->type)};
    // the directory is found from the path the file has now, if it still has one
    char path[MAX_PATH];
    if (fd->mount->fs->getpath != NULL && fd->mount->fs->getpath(fd, path) == 0) {
        struct inotify_subject by_path;
        inotify_subject(fd->mount, path, &by_path);
        if (by_path.self == subject.self) {
            subject.parent = by_path.parent;
            strcpy(subject.name, by_path.name);
        }
    }
    inotify_event(&subject, mask, mask, 0);
}

uint32_t inotify_new_cookie(void) {
    return ++next_cookie;
}

int_t sys_inotify_init1(int_t flags) {
    STRACE("inotify_init1(%#x)", flags);
    if (flags & ~(O_CLOEXEC_ | O_NONBLOCK_))
        return _EINVAL;
    struct inotify *ino = calloc(1, sizeof(struct inotify));
    if (ino == NULL)
        return _ENOMEM;
    struct fd *fd = adhoc_fd_create(&inotify_ops);
    if (fd == NULL) {
        free(ino);
        return _ENOMEM;
    }
    list_init(&ino->watches);
    list_init(&ino->events);
    ino->fd = fd;
    fd->inotifyfd.ino = ino;
    lock(&inotify_lock);
    list_add(&instances, &ino->instances);
    unlock(&inotify_lock);
    return f_install(fd, flags);
}

int_t sys_inotify_init(void) {
    return sys_inotify_init1(0);
}

static struct inotify *inotify_get(fd_t f) {
    struct fd *fd = f_get(f);
    if (fd == NULL)
        return ERR_PTR(_EBADF);
    if (fd->ops != &inotify_ops)
        return ERR_PTR(_EINVAL);
    return fd->inotifyfd.ino;
}

int_t sys_inotify_add_watch(fd_t f, addr_t path_addr, uint_t mask) {
    char raw[MAX_PATH];
    if (user_read_string(path_addr, raw, sizeof(raw)))
        return _EFAULT;
    STRACE("inotify_add_watch(%d, \"%s\", %#x)", f, raw, mask);
    struct inotify *ino = inotify_get(f);
    if (IS_ERR(ino))
        return PTR_ERR(ino);
    if ((mask & IN_ALL_EVENTS_) == 0)
        return _EINVAL;
    if ((mask & IN_MASK_ADD_) && (mask & IN_MASK_CREATE_))
        return _EINVAL;

    bool follow = !(mask & IN_DONT_FOLLOW_);
    char path[MAX_PATH];
    int err = path_normalize(AT_PWD, raw, path, follow ? N_SYMLINK_FOLLOW : N_SYMLINK_NOFOLLOW);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    struct statbuf stat;
    err = mount->fs->stat(mount, path, &stat);
    mount_release(mount);
    if (err < 0)
        return err;
    if ((mask & IN_ONLYDIR_) && !S_ISDIR(stat.mode))
        return _ENOTDIR;

    lock(&inotify_lock);
    struct watch *watch;
    list_for_each_entry(&ino->watches, watch, list) {
        if (watch->ino != stat.inode || watch->mount != mount)
            continue;
        int wd = watch->wd;
        if (mask & IN_MASK_CREATE_)
            wd = _EEXIST;
        else if (mask & IN_MASK_ADD_)
            watch->mask |= mask;
        else
            watch->mask = mask;
        unlock(&inotify_lock);
        return wd;
    }
    watch = malloc(sizeof(struct watch));
    if (watch == NULL) {
        unlock(&inotify_lock);
        return _ENOMEM;
    }
    watch->wd = ++ino->next_wd;
    watch->mask = mask;
    watch->mount = mount;
    watch->ino = stat.inode;
    list_add_tail(&ino->watches, &watch->list);
    inotify_watches++;
    int wd = watch->wd;
    unlock(&inotify_lock);
    return wd;
}

int_t sys_inotify_rm_watch(fd_t f, int_t wd) {
    STRACE("inotify_rm_watch(%d, %d)", f, wd);
    struct inotify *ino = inotify_get(f);
    if (IS_ERR(ino))
        return PTR_ERR(ino);
    lock(&inotify_lock);
    struct watch *watch;
    list_for_each_entry(&ino->watches, watch, list) {
        if (watch->wd == wd) {
            watch_remove(ino, watch);
            unlock(&inotify_lock);
            return 0;
        }
    }
    unlock(&inotify_lock);
    return _EINVAL;
}

// what a program reads: struct inotify_event
struct inotify_event_ {
    int32_t wd;
    uint32_t mask;
    uint32_t cookie;
    uint32_t len;
};

static ssize_t inotify_read(struct fd *fd, void *buf, size_t bufsize) {
    struct inotify *ino = fd->inotifyfd.ino;
    lock(&inotify_lock);
    while (list_empty(&ino->events)) {
        if (fd->flags & O_NONBLOCK_) {
            unlock(&inotify_lock);
            return _EAGAIN;
        }
        if (wait_for(&fd->cond, &inotify_lock, NULL)) {
            unlock(&inotify_lock);
            return _EINTR;
        }
    }
    size_t done = 0;
    while (!list_empty(&ino->events)) {
        struct event *ev = list_first_entry(&ino->events, struct event, list);
        size_t size = sizeof(struct inotify_event_) + ev->len;
        if (size > bufsize - done)
            break;
        struct inotify_event_ out = {.wd = ev->wd, .mask = ev->mask, .cookie = ev->cookie, .len = ev->len};
        memcpy((char *) buf + done, &out, sizeof(out));
        memcpy((char *) buf + done + sizeof(out), ev->name, ev->len);
        done += size;
        list_remove(&ev->list);
        free(ev);
        ino->nevents--;
    }
    ino->queued = ino->nevents;
    unlock(&inotify_lock);
    // the buffer cannot hold even the first event
    if (done == 0)
        return _EINVAL;
    return (ssize_t) done;
}

// no lock: called with poll locks held
static int inotify_poll(struct fd *fd) {
    return fd->inotifyfd.ino->queued ? POLL_READ : 0;
}

static int inotify_close(struct fd *fd) {
    struct inotify *ino = fd->inotifyfd.ino;
    lock(&inotify_lock);
    list_remove(&ino->instances);
    struct watch *watch, *wtmp;
    list_for_each_entry_safe(&ino->watches, watch, wtmp, list) {
        list_remove(&watch->list);
        free(watch);
        inotify_watches--;
    }
    struct event *ev, *etmp;
    list_for_each_entry_safe(&ino->events, ev, etmp, list) {
        list_remove(&ev->list);
        free(ev);
    }
    unlock(&inotify_lock);
    free(ino);
    return 0;
}

static struct fd_ops inotify_ops = {
    .read = inotify_read,
    .poll = inotify_poll,
    .close = inotify_close,
};
