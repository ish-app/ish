#ifndef UTIL_TIMER_H
#define UTIL_TIMER_H

#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "util/sync.h"

static inline struct timespec timespec_now(clockid_t clockid) {
    assert(clockid == CLOCK_MONOTONIC || clockid == CLOCK_REALTIME);
    struct timespec now;
    clock_gettime(clockid, &now); // can't fail, according to posix spec
    return now;
}

static inline struct timespec timespec_add(struct timespec x, struct timespec y) {
    x.tv_sec += y.tv_sec;
    x.tv_nsec += y.tv_nsec;
    if (x.tv_nsec >= 1000000000) {
        x.tv_nsec -= 1000000000;
        x.tv_sec++;
    }
    return x;
}

static inline struct timespec timespec_subtract(struct timespec x, struct timespec y) {
    struct timespec result;
    if (x.tv_nsec < y.tv_nsec) {
        x.tv_sec -= 1;
        x.tv_nsec += 1000000000;
    }
    result.tv_sec = x.tv_sec - y.tv_sec;
    result.tv_nsec = x.tv_nsec - y.tv_nsec;
    return result;
}

static inline bool timespec_is_zero(struct timespec ts) {
    return ts.tv_sec == 0 && ts.tv_nsec == 0;
}

static inline bool timespec_positive(struct timespec ts) {
    return ts.tv_sec > 0 || (ts.tv_sec == 0 && ts.tv_nsec > 0);
}

static inline struct timespec timespec_normalize(struct timespec ts) {
    ts.tv_sec += ts.tv_nsec / 1000000000;
    ts.tv_nsec %= 1000000000;
    return ts;
}

typedef void (*timer_callback_t)(void *data);
struct timer {
    clockid_t clockid;
    struct timespec start;
    struct timespec end;
    struct timespec interval;

    bool active;
    bool thread_running;
    pthread_t thread;
    timer_callback_t callback;
    void *data;
    lock_t lock;

    bool dead; // set by timer_free, the thread will free the timer if this is set when it finishes
};

struct timer *timer_new(clockid_t clockid, timer_callback_t callback, void *data);
void timer_free(struct timer *timer);
// value is how long to wait until the next fire
// interval is how long after that to wait until the next fire (if non-zero)
// bizzare interface is based off setitimer, because this is going to be used
// to implement setitimer
struct timer_spec {
    struct timespec value;
    struct timespec interval;
};
int timer_set(struct timer *timer, struct timer_spec spec, struct timer_spec *oldspec);

#endif
