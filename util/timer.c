#include <stdlib.h>
#include <time.h>
#include "util/timer.h"

struct timer *timer_new(timer_callback_t callback, void *data) {
    struct timer *timer = malloc(sizeof(struct timer));
    timer->callback = callback;
    timer->data = data;
    timer->running = false;
    return timer;
}

void timer_free(struct timer *timer) {
    free(timer);
}

static void *timer_thread(struct timer *timer) {
    while (true) {
        struct timespec remaining = timespec_subtract(timer->end, timespec_now());
        while (timespec_positive(remaining)) {
            nanosleep(&remaining, NULL);
            remaining = timespec_subtract(timer->end, timespec_now());
        }
        if (timespec_positive(timer->interval)) {
            timer->start = timespec_now();
            timer->end = timespec_add(timer->start, timer->interval);
        } else {
            return NULL;
        }
    }
}

int timer_set(struct timer *timer, struct timer_spec spec, struct timer_spec *oldspec) {
    struct timespec now = timespec_now();
    if (oldspec != NULL) {
        oldspec->value = timespec_subtract(timer->end, now);
        oldspec->interval = timer->interval;
    }

    timer->start = now;
    timer->end = timespec_add(timer->start, spec.value);
    timer->interval = spec.interval;
    if (!timespec_is_zero(spec.value) && !timespec_is_zero(spec.interval)) {
        if (!timer->running) {
            pthread_create(&timer->thread, NULL, (void *(*)(void*)) timer_thread, timer);
            timer->running = true;
        }
    } else {
        if (timer->running) {
            pthread_cancel(timer->thread);
            timer->running = false;
        }
    }
    return 0;
}
