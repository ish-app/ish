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
#include "kernel/time.h"
#include "fs/poll.h"

static int clockid_to_real(uint_t clock, clockid_t *real) {
    switch (clock) {
        case CLOCK_REALTIME_:
        case CLOCK_REALTIME_COARSE_:
            *real = CLOCK_REALTIME; break;
        case CLOCK_MONOTONIC_: *real = CLOCK_MONOTONIC; break;
        default: return _EINVAL;
    }
    return 0;
}

static struct timer_spec timer_spec_to_real(struct itimerspec_ itspec) {
    struct timer_spec spec = {
        .value.tv_sec = itspec.value.sec,
        .value.tv_nsec = itspec.value.nsec,
        .interval.tv_sec = itspec.interval.sec,
        .interval.tv_nsec = itspec.interval.nsec,
    };
    return spec;
};

static struct itimerspec_ timer_spec_from_real(struct timer_spec spec) {
    struct itimerspec_ itspec = {
        .value.sec = spec.value.tv_sec,
        .value.nsec = spec.value.tv_nsec,
        .interval.sec = spec.interval.tv_sec,
        .interval.nsec = spec.interval.tv_nsec,
    };
    return itspec;
};

dword_t sys_time(addr_t time_out) {
    dword_t now = time(NULL);
    if (time_out != 0)
        if (user_put(time_out, now))
            return _EFAULT;
    return now;
}

dword_t sys_stime(addr_t UNUSED(time)) {
    return _EPERM;
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
        if (clockid_to_real(clock, &clock_id)) return _EINVAL;
        int err = clock_gettime(clock_id, &ts);
        if (err < 0)
            return errno_map();
    }
    struct timespec_ t;
    t.sec = ts.tv_sec;
    t.nsec = ts.tv_nsec;
    if (user_put(tp, t))
        return _EFAULT;
    STRACE(" {%lds %ldns}", t.sec, t.nsec);
    return 0;
}

dword_t sys_clock_getres(dword_t clock, addr_t res_addr) {
    STRACE("clock_getres(%d, %#x)", clock, res_addr);
    clockid_t clock_id;
    if (clockid_to_real(clock, &clock_id)) return _EINVAL;

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
    struct siginfo_ info = {
        .code = SI_TIMER_,
    };
    send_signal(task, SIGALRM_, info);
}

static int itimer_set(struct tgroup *group, int which, struct timer_spec spec, struct timer_spec *old_spec) {
    if (which != ITIMER_REAL_) {
        FIXME("unimplemented setitimer %d", which);
        return _EINVAL;
    }

    if (!group->itimer) {
        struct timer *timer = timer_new(CLOCK_REALTIME, (timer_callback_t) itimer_notify, current);
        if (IS_ERR(timer))
            return PTR_ERR(timer);
        group->itimer = timer;
    }

    return timer_set(group->itimer, spec, old_spec);
}

int_t sys_setitimer(int_t which, addr_t new_val_addr, addr_t old_val_addr) {
    struct itimerval_ val;
    if (user_get(new_val_addr, val))
        return _EFAULT;
    STRACE("setitimer(%d, {%ds %dus, %ds %dus}, 0x%x)", which, val.value.sec, val.value.usec, val.interval.sec, val.interval.usec, old_val_addr);

    struct timer_spec spec = {
        .interval.tv_sec = val.interval.sec,
        .interval.tv_nsec = val.interval.usec * 1000,
        .value.tv_sec = val.value.sec,
        .value.tv_nsec = val.value.usec * 1000,
    };
    struct timer_spec old_spec;

    struct tgroup *group = current->group;
    lock(&group->lock);
    int err = itimer_set(group, which, spec, &old_spec);
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

uint_t sys_alarm(uint_t seconds) {
    STRACE("alarm(%d)", seconds);
    struct timer_spec spec = {
        .value.tv_sec = seconds,
    };
    struct timer_spec old_spec;

    struct tgroup *group = current->group;
    lock(&group->lock);
    int err = itimer_set(group, ITIMER_REAL_, spec, &old_spec);
    unlock(&group->lock);
    if (err < 0)
        return err;

    // Round up, and make sure to not return 0 if old_spec is > 0
    seconds = old_spec.value.tv_sec;
    if (old_spec.value.tv_nsec >= 500000000)
        seconds++;
    if (seconds == 0 && !timespec_is_zero(old_spec.value))
        seconds = 1;
    return seconds;
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
        tmp.tms_utime = clock_from_timeval(rusage.utime);
        tmp.tms_stime = clock_from_timeval(rusage.stime);
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

static void posix_timer_callback(struct posix_timer *timer) {
    if (timer->tgroup == NULL)
        return;
    struct siginfo_ info = {
        .code = SI_TIMER_,
        .timer.timer = timer->timer_id,
        .timer.overrun = 0,
        .timer.value = timer->sig_value,
    };
    lock(&pids_lock);
    struct task *thread = pid_get_task(timer->thread_pid);
    // TODO: solve pid reuse. currently we have two ways of referring to a task: pid_t_ and struct task *. pids get reused. task struct pointers get freed on exit or reap. need a third option for cases like this, like a refcount layer.
    if (thread != NULL)
        send_signal(thread, timer->signal, info);
    unlock(&pids_lock);
}

#define SIGEV_SIGNAL_ 0
#define SIGEV_NONE_ 1
#define SIGEV_THREAD_ID_ 4

int_t sys_timer_create(dword_t clock, addr_t sigevent_addr, addr_t timer_addr) {
    STRACE("timer_create(%d, %#x, %#x)", clock, sigevent_addr, timer_addr);
    clockid_t real_clockid;
    if (clockid_to_real(clock, &real_clockid))
        return _EINVAL;
    struct sigevent_ sigev;
    if (user_get(sigevent_addr, sigev))
        return _EFAULT;
    if (sigev.method != SIGEV_SIGNAL_ && sigev.method != SIGEV_NONE_ && sigev.method != SIGEV_THREAD_ID_)
        return _EINVAL;

    if (sigev.method == SIGEV_THREAD_ID_) {
        lock(&pids_lock);
        if (pid_get_task(sigev.tid) == NULL)
            return _EINVAL;
        unlock(&pids_lock);
    }

    struct tgroup *group = current->group;
    lock(&group->lock);
    unsigned timer_id;
    for (timer_id = 0; timer_id < TIMERS_MAX; timer_id++) {
        if (group->posix_timers[timer_id].timer == NULL)
            break;
    }
    if (timer_id >= TIMERS_MAX) {
        unlock(&group->lock);
        return _ENOMEM;
    }
    if (user_put(timer_addr, timer_id)) {
        unlock(&group->lock);
        return _EFAULT;
    }

    struct posix_timer *timer = &group->posix_timers[timer_id];
    timer->timer_id = timer_id;
    timer->timer = timer_new(real_clockid, (timer_callback_t) posix_timer_callback, timer);
    timer->signal = sigev.signo;
    timer->sig_value = sigev.value;
    timer->tgroup = NULL;
    if (sigev.method == SIGEV_SIGNAL_) {
        timer->tgroup = group;
        timer->thread_pid = 0;
    } else if (sigev.method == SIGEV_THREAD_ID_) {
        timer->tgroup = group;
        timer->thread_pid = group->leader->pid;
    }
    unlock(&group->lock);
    return 0;
}

#define TIMER_ABSTIME_ (1 << 0)

int_t sys_timer_settime(dword_t timer_id, int_t flags, addr_t new_value_addr, addr_t old_value_addr) {
    STRACE("timer_settime(%d, %d, %#x, %#x)", timer_id, flags, new_value_addr, old_value_addr);
    struct itimerspec_ value;
    if (user_get(new_value_addr, value))
        return _EFAULT;
    if (timer_id > TIMERS_MAX)
        return _EINVAL;

    lock(&current->group->lock);
    struct posix_timer *timer = &current->group->posix_timers[timer_id];
    struct timer_spec spec = timer_spec_to_real(value);
    struct timer_spec old_spec;
    if (flags & TIMER_ABSTIME_) {
        struct timespec now = timespec_now(timer->timer->clockid);
        spec.value = timespec_subtract(spec.value, now);
    }
    int err = timer_set(timer->timer, spec, &old_spec);
    unlock(&current->group->lock);
    if (err < 0)
        return err;

    if (old_value_addr) {
        struct itimerspec_ old_value = timer_spec_from_real(old_spec);
        if (user_put(old_value_addr, old_value))
            return _EFAULT;
    }
    return 0;
}

int_t sys_timer_delete(dword_t timer_id) {
    STRACE("timer_delete(%d)\n", timer_id);
    lock(&current->group->lock);
    struct posix_timer *timer = &current->group->posix_timers[timer_id];
    if (timer->timer == NULL) {
        unlock(&current->group->lock);
        return _EINVAL;
    }
    timer_free(timer->timer);
    timer->timer = NULL;
    unlock(&current->group->lock);
    return 0;
}

static struct fd_ops timerfd_ops;

static void timerfd_callback(struct fd *fd) {
    lock(&fd->lock);
    fd->timerfd.expirations++;
    notify(&fd->cond);
    unlock(&fd->lock);
    poll_wakeup(fd, POLL_READ);
}

fd_t sys_timerfd_create(int_t clockid, int_t flags) {
    STRACE("timerfd_create(%d, %#x)", clockid, flags);
    clockid_t real_clockid;
    if (clockid_to_real(clockid, &real_clockid)) return _EINVAL;

    struct fd *fd = adhoc_fd_create(&timerfd_ops);
    if (fd == NULL)
        return _ENOMEM;

    fd->timerfd.timer = timer_new(real_clockid, (timer_callback_t) timerfd_callback, fd);
    return f_install(fd, flags);
}

int_t sys_timerfd_settime(fd_t f, int_t flags, addr_t new_value_addr, addr_t old_value_addr) {
    STRACE("timerfd_settime(%d, %d, %#x, %#x)", f, flags, new_value_addr, old_value_addr);
    if (flags & ~(TIMER_ABSTIME_))
        return _EINVAL;
    struct fd *fd = f_get(f);
    if (fd == NULL)
        return _EBADF;
    if (fd->ops != &timerfd_ops)
        return _EINVAL;
    struct itimerspec_ value;
    if (user_get(new_value_addr, value))
        return _EFAULT;
    struct timer_spec spec = timer_spec_to_real(value);
    struct timer_spec old_spec;
    if (flags & TIMER_ABSTIME_) {
        struct timespec now = timespec_now(fd->timerfd.timer->clockid);
        spec.value = timespec_subtract(spec.value, now);
    }

    lock(&fd->lock);
    int err = timer_set(fd->timerfd.timer, spec, &old_spec);
    unlock(&fd->lock);
    if (err < 0)
        return err;

    if (old_value_addr) {
        struct itimerspec_ old_value = timer_spec_from_real(old_spec);
        if (user_put(old_value_addr, old_value))
            return _EFAULT;
    }

    return 0;
}

static ssize_t timerfd_read(struct fd *fd, void *buf, size_t bufsize) {
    if (bufsize < sizeof(uint64_t))
        return _EINVAL;
    lock(&fd->lock);
    while (fd->timerfd.expirations == 0) {
        if (fd->flags & O_NONBLOCK_) {
            unlock(&fd->lock);
            return _EAGAIN;
        }
        int err = wait_for(&fd->cond, &fd->lock, NULL);
        if (err < 0) {
            unlock(&fd->lock);
            return err;
        }
    }

    *(uint64_t *) buf = fd->timerfd.expirations;
    fd->timerfd.expirations = 0;
    unlock(&fd->lock);
    return sizeof(uint64_t);
}
static int timerfd_poll(struct fd *fd) {
    int res = 0;
    lock(&fd->lock);
    if (fd->timerfd.expirations != 0)
        res |= POLL_READ;
    unlock(&fd->lock);
    return res;
}
static int timerfd_close(struct fd *fd) {
    timer_free(fd->timerfd.timer);
    return 0;
}

static struct fd_ops timerfd_ops = {
    .read = timerfd_read,
    .poll = timerfd_poll,
    .close = timerfd_close,
};
