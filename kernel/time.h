#ifndef TIME_H
#define TIME_H
#include "misc.h"

dword_t sys_time(addr_t time_out);
#define CLOCK_REALTIME_ 0
#define CLOCK_MONOTONIC_ 1
dword_t sys_clock_gettime(dword_t clock, addr_t tp);

struct timeval_ {
    dword_t sec;
    dword_t usec;
};
struct timespec_ {
    dword_t sec;
    dword_t nsec;
};

#define ITIMER_REAL_ 0
#define ITIMER_VIRTUAL_ 1
#define ITIMER_PROF_ 2
struct itimerval_ {
    struct timeval_ interval;
    struct timeval_ value;
};

struct tms_ {
    dword_t tms_utime;  /* user time */
    dword_t tms_stime;  /* system time */
    dword_t tms_cutime; /* user time of children */
    dword_t tms_cstime; /* system time of children */
};

dword_t sys_getitimer(dword_t which, addr_t val);
dword_t sys_setitimer(dword_t which, addr_t new_val, addr_t old_val);
dword_t sys_times( addr_t tbuf);
dword_t sys_nanosleep(addr_t req, addr_t rem);

#endif
