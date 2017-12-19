#include <stdlib.h>
#include <time.h>
#include "util/timer.h"
#include "misc.h"

struct timer *timer_new(timer_callback_t callback, void *data) {
    struct timer *timer = malloc(sizeof(struct timer));
    timer->callback = callback;
    timer->data = data;
    timer->running = false;
    lock_init(timer);
    return timer;
}

void timer_free(struct timer *timer) {
    free(timer);
}

static void *timer_thread(void *param) {
    struct timer *timer = param;
    lock(timer);
    while (true) {
        struct timespec remaining = timespec_subtract(timer->end, timespec_now());
        while (timespec_positive(remaining)) {
            unlock(timer);
            nanosleep(&remaining, NULL);
            lock(timer);
            remaining = timespec_subtract(timer->end, timespec_now());
        }
        timer->callback(timer->data);
        if (timespec_positive(timer->interval)) {
            timer->start = timespec_now();
            timer->end = timespec_add(timer->start, timer->interval);
        } else {
            unlock(timer);
            return NULL;
        }
    }
}

int timer_set(struct timer *timer, struct timer_spec spec, struct timer_spec *oldspec) {
    lock(timer);
    struct timespec now = timespec_now();
    if (oldspec != NULL) {
        oldspec->value = timespec_subtract(timer->end, now);
        oldspec->interval = timer->interval;
    }

    timer->start = now;
    timer->end = timespec_add(timer->start, spec.value);
    timer->interval = spec.interval;
    if (!timespec_is_zero(spec.value)) {
        if (!timer->running) {
            pthread_create(&timer->thread, NULL, timer_thread, timer);
            pthread_detach(timer->thread);
            timer->running = true;
        }
    } else {
        if (timer->running) {
            pthread_cancel(timer->thread);
            timer->running = false;
        }
    }
    unlock(timer);
    return 0;
}
