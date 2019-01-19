#ifdef __linux__
#define _GNU_SOURCE
#include <sys/resource.h>
#endif
#include "debug.h"
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "kernel/resource.h"
#include "fs/poll.h"

dword_t sys_time(addr_t time_out) {
    dword_t now = time(NULL);
    if (time_out != 0)
        if (user_put(time_out, now))
            return _EFAULT;
    return now;
}

dword_t sys_clock_gettime(dword_t clock, addr_t tp) {
    STRACE("clock_gettime(%d, 0x%x)", clock, tp);

    struct timespec ts;
    if (clock == CLOCK_PROCESS_CPUTIME_ID_) {
        // FIXME this is thread usage, not process usage
        struct rusage_ rusage = rusage_get_current();
        ts.tv_sec = rusage.utime.sec;
        ts.tv_nsec = rusage.utime.usec * 1000;
    } else {
        clockid_t clock_id;
        switch (clock) {
            case CLOCK_REALTIME_: clock_id = CLOCK_REALTIME; break;
            case CLOCK_MONOTONIC_: clock_id = CLOCK_MONOTONIC; break;
            default: return _EINVAL;
        }
        int err = clock_gettime(clock_id, &ts);
        if (err < 0)
            return errno_map();
    }
    struct timespec_ t;
    t.sec = ts.tv_sec;
    t.nsec = ts.tv_nsec;
    if (user_put(tp, t))
        return _EFAULT;
    return 0;
}

dword_t sys_clock_getres(dword_t clock, addr_t res_addr) {
    STRACE("clock_getres(%d, %#x)", clock, res_addr);
    clockid_t clock_id;
    switch (clock) {
        case CLOCK_REALTIME_: clock_id = CLOCK_REALTIME; break;
        case CLOCK_MONOTONIC_: clock_id = CLOCK_MONOTONIC; break;
        default: return _EINVAL;
    }

    struct timespec res;
    int err = clock_getres(clock_id, &res);
    if (err < 0)
        return errno_map();
    struct timespec_ t;
    t.sec = res.tv_sec;
    t.nsec = res.tv_nsec;
    if (user_put(res_addr, t))
        return _EFAULT;
    return 0;
}

dword_t sys_clock_settime(dword_t UNUSED(clock), addr_t UNUSED(tp)) {
    return _EPERM;
}

static void itimer_notify(struct task *task) {
    send_signal(task, SIGALRM_);
}

dword_t sys_setitimer(dword_t which, addr_t new_val_addr, addr_t old_val_addr) {
    if (which != ITIMER_REAL_) {
        FIXME("unimplemented setitimer %d", which);
        return _EINVAL;
    }

    struct itimerval_ val;
    if (user_get(new_val_addr, val))
        return _EFAULT;

    STRACE("setitimer({%ds %dus, %ds %dus}, 0x%x)", val.value.sec, val.value.usec, val.interval.sec, val.interval.usec, old_val_addr);
    struct tgroup *group = current->group;
    lock(&group->lock);
    if (!group->timer) {
        struct timer *timer = timer_new((timer_callback_t) itimer_notify, current);
        if (IS_ERR(timer)) {
            unlock(&group->lock);
            return PTR_ERR(timer);
        }
        group->timer = timer;
    }

    struct timer_spec spec;
    spec.interval.tv_sec = val.interval.sec;
    spec.interval.tv_nsec = val.interval.usec * 1000;
    spec.value.tv_sec = val.value.sec;
    spec.value.tv_nsec = val.value.usec * 1000;
    struct timer_spec old_spec;
    int err = timer_set(group->timer, spec, &old_spec);
    unlock(&group->lock);
    if (err < 0)
        return err;

    if (old_val_addr != 0) {
        struct itimerval_ old_val;
        old_val.interval.sec = old_spec.interval.tv_sec;
        old_val.interval.usec = old_spec.interval.tv_nsec / 1000;
        old_val.value.sec = old_spec.value.tv_sec;
        old_val.value.usec = old_spec.value.tv_nsec / 1000;
        if (user_put(old_val_addr, old_val))
            return _EFAULT;
    }

    return 0;
}

dword_t sys_nanosleep(addr_t req_addr, addr_t rem_addr) {
    struct timespec_ req_ts;
    if (user_get(req_addr, req_ts))
        return _EFAULT;
    STRACE("nanosleep({%d, %d}, 0x%x", req_ts.sec, req_ts.nsec, rem_addr);
    struct timespec req;
    req.tv_sec = req_ts.sec;
    req.tv_nsec = req_ts.nsec;
    struct timespec rem;
    if (nanosleep(&req, &rem) < 0)
        return errno_map();
    if (rem_addr != 0) {
        struct timespec_ rem_ts;
        rem_ts.sec = rem.tv_sec;
        rem_ts.nsec = rem.tv_nsec;
        if (user_put(rem_addr, rem_ts))
            return _EFAULT;
    }
    return 0;
}

dword_t sys_times(addr_t tbuf) {
    STRACE("times(0x%x)", tbuf);
    if (tbuf) {
        struct tms_ tmp;
        struct rusage_ rusage = rusage_get_current();
        tmp.tms_utime = (rusage.utime.sec * 100) + (rusage.utime.usec/10000);
        tmp.tms_stime = (rusage.utime.sec * 100) + (rusage.utime.usec/10000);
        tmp.tms_cutime = tmp.tms_utime;
        tmp.tms_cstime = tmp.tms_stime;
        if (user_put(tbuf, tmp))
            return _EFAULT;
    }
    return 0;
}

dword_t sys_gettimeofday(addr_t tv, addr_t tz) {
    STRACE("gettimeofday(0x%x, 0x%x)", tv, tz);
    struct timeval timeval;
    struct timezone timezone;
    if (gettimeofday(&timeval, &timezone) < 0) {
        return errno_map();
    }
    struct timeval_ tv_;
    struct timezone_ tz_;
    tv_.sec = timeval.tv_sec;
    tv_.usec = timeval.tv_usec;
    tz_.minuteswest = timezone.tz_minuteswest;
    tz_.dsttime = timezone.tz_dsttime;
    if ((tv && user_put(tv, tv_)) || (tz && user_put(tz, tz_))) {
        return _EFAULT;
    }
    return 0;
}

dword_t sys_settimeofday(addr_t UNUSED(tv), addr_t UNUSED(tz)) {
    return _EPERM;
}

static struct fd_ops timerfd_ops;

static void timerfd_callback(struct fd *fd) {
    lock(&fd->lock);
    fd->expirations++;
    notify(&fd->cond);
    unlock(&fd->lock);
    poll_wakeup(fd);
}

fd_t sys_timerfd_create(int_t clockid, int_t flags) {
    STRACE("timerfd_create(%d, %#x)", clockid, flags);
    if (clockid != ITIMER_REAL_) {
        FIXME("timerfd %d", clockid);
        return _EINVAL;
    }

    struct fd *fd = adhoc_fd_create(&timerfd_ops);
    if (fd == NULL)
        return _ENOMEM;

    fd->timer = timer_new((timer_callback_t) timerfd_callback, fd);
    return f_install(fd, flags);
}

static ssize_t timerfd_read(struct fd *fd, void *buf, size_t bufsize) {
    if (bufsize < sizeof(uint64_t))
        return _EINVAL;
    lock(&fd->lock);
    while (fd->expirations == 0) {
        if (fd->flags & O_NONBLOCK_) {
            unlock(&fd->lock);
            return _EAGAIN;
        }
        wait_for(&fd->cond, &fd->lock, NULL);
    }

    *(uint64_t *) buf = fd->expirations;
    fd->expirations = 0;
    unlock(&fd->lock);
    return sizeof(uint64_t);
}
static int timerfd_poll(struct fd *fd) {
    int res = 0;
    lock(&fd->lock);
    if (fd->expirations == 0)
        res |= POLL_READ;
    unlock(&fd->lock);
    return res;
}
static int timerfd_close(struct fd *fd) {
    timer_free(fd->timer);
    return 0;
}

static struct fd_ops timerfd_ops = {
    .read = timerfd_read,
    .poll = timerfd_poll,
    .close = timerfd_close,
};
