#include <time.h>
#include <signal.h>
#include "sys/calls.h"
#include "sys/errno.h"

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
        return err_map(errno);
    struct time_spec t;
    t.sec = ts.tv_sec;
    t.nsec = ts.tv_nsec;
    if (user_put(tp, t))
        return _EFAULT;
    return 0;
}

static void itimer_notify(union sigval sv) {
    struct process *proc = sv.sival_ptr;
    send_signal(proc, SIGALRM_);
}

dword_t sys_setitimer(dword_t which, addr_t new_val_addr, addr_t old_val_addr) {
    if (which != ITIMER_REAL_)
        TODO("setitimer %d", which);

    struct itimerval_ val;
    if (user_get(new_val_addr, val))
        return _EFAULT;

    if (!current->has_timer) {
        struct sigevent sigev;
        sigev.sigev_notify = SIGEV_THREAD;
        sigev.sigev_notify_function = itimer_notify;
        sigev.sigev_value.sival_ptr = current;
        if (timer_create(CLOCK_REALTIME, &sigev, &current->timer) < 0)
            return err_map(errno);
        current->has_timer = true;
    }

    struct itimerspec spec;
    spec.it_interval.tv_sec = val.interval.sec;
    spec.it_interval.tv_nsec = val.interval.usec * 1000;
    spec.it_value.tv_sec = val.value.sec;
    spec.it_value.tv_nsec = val.value.usec * 1000;
    struct itimerspec old_spec;
    if (timer_settime(current->timer, 0, &spec, &old_spec) < 0)
        return err_map(errno);

    if (old_val_addr != 0) {
        struct itimerval_ old_val;
        old_val.interval.sec = old_spec.it_interval.tv_sec;
        old_val.interval.usec = old_spec.it_interval.tv_nsec / 1000;
        old_val.value.sec = old_spec.it_value.tv_sec;
        old_val.value.usec = old_spec.it_value.tv_nsec / 1000;
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
        return err_map(errno);
    if (rem_addr != 0) {
        struct timespec_ rem_ts;
        rem_ts.sec = rem.tv_sec;
        rem_ts.nsec = rem.tv_nsec;
        if (user_put(rem_addr, rem_ts))
            return _EFAULT;
    }
    return 0;
}
