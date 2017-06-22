#include <time.h>
#include "sys/calls.h"
#include "sys/errno.h"

dword_t sys_time(addr_t time_out) {
    dword_t now = time(NULL);
    if (time_out != 0)
        user_put_count(time_out, &now, sizeof(now));
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
    user_put_count(tp, &t, sizeof(t));
    return 0;
}
