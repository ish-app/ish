#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include "util/timer.h"
#include "misc.h"

struct timer *timer_new(timer_callback_t callback, void *data) {
    struct timer *timer = malloc(sizeof(struct timer));
    timer->callback = callback;
    timer->data = data;
    timer->running = false;
    timer->dead = false;
    lock_init(&timer->lock);
    return timer;
}

void timer_free(struct timer *timer) {
    lock(&timer->lock);
    if (timer->running) {
        timer->running = false;
        timer->dead = true;
        pthread_kill(timer->thread, SIGUSR1);
        unlock(&timer->lock);
    } else {
        unlock(&timer->lock);
        if (!timer->dead)
            free(timer);
    }
}

static void *timer_thread(void *param) {
    struct timer *timer = param;
    lock(&timer->lock);
    while (true) {
        struct timespec remaining = timespec_subtract(timer->end, timespec_now());
        while (timer->running && timespec_positive(remaining)) {
            unlock(&timer->lock);
            nanosleep(&remaining, NULL);
            lock(&timer->lock);
            remaining = timespec_subtract(timer->end, timespec_now());
        }
        if (timer->running)
            timer->callback(timer->data);
        if (timespec_positive(timer->interval)) {
            timer->start = timer->end;
            timer->end = timespec_add(timer->start, timer->interval);
        } else {
            break;
        }
    }
    timer->running = false;
    if (timer->dead)
        free(timer);
    else
        unlock(&timer->lock);
    return NULL;
}

int timer_set(struct timer *timer, struct timer_spec spec, struct timer_spec *oldspec) {
    lock(&timer->lock);
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
            timer->running = true;
            pthread_create(&timer->thread, NULL, timer_thread, timer);
            pthread_detach(timer->thread);
        }
    } else {
        if (timer->running) {
            timer->running = false;
            pthread_kill(timer->thread, SIGUSR1);
        }
    }
    unlock(&timer->lock);
    return 0;
}
