#include "debug.h"
#include <time.h>
#include <signal.h>
#include "kernel/calls.h"
#include "kernel/errno.h"

dword_t sys_time(addr_t time_out) {
    dword_t now = time(NULL);
    if (time_out != 0)
        if (user_put(time_out, now))
            return _EFAULT;
    return now;
}

dword_t sys_clock_gettime(dword_t clock, addr_t tp) {
    clockid_t clock_id;
    switch (clock) {
        case CLOCK_REALTIME_: clock_id = CLOCK_REALTIME; break;
        case CLOCK_MONOTONIC_: clock_id = CLOCK_MONOTONIC; break;
        default: return _EINVAL;
    }

    struct timespec ts;
    int err = clock_gettime(clock_id, &ts);
    if (err < 0)
        return errno_map();
    struct timespec_ t;
    t.sec = ts.tv_sec;
    t.nsec = ts.tv_nsec;
    if (user_put(tp, t))
        return _EFAULT;
    return 0;
}

static void itimer_notify(struct process *proc) {
    send_signal(proc, SIGALRM_);
}

dword_t sys_setitimer(dword_t which, addr_t new_val_addr, addr_t old_val_addr) {
    STRACE("setitimer");
    if (which != ITIMER_REAL_)
        TODO("setitimer %d", which);

    struct itimerval_ val;
    if (user_get(new_val_addr, val))
        return _EFAULT;

    if (!current->has_timer) {
        current->timer = timer_new((timer_callback_t) itimer_notify, current);
        if (IS_ERR(current->timer))
            return PTR_ERR(current->timer);
        current->has_timer = true;
    }

    struct timer_spec spec;
    spec.interval.tv_sec = val.interval.sec;
    spec.interval.tv_nsec = val.interval.usec * 1000;
    spec.value.tv_sec = val.value.sec;
    spec.value.tv_nsec = val.value.usec * 1000;
    struct timer_spec old_spec;
    if (timer_set(current->timer, spec, &old_spec) < 0)
        return errno_map();

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
