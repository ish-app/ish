#ifndef PLATFORM_H
#define PLATFORM_H
#include "misc.h"

// for some reason a tick is always 10ms
struct cpu_usage {
    uint64_t user_ticks;
    uint64_t system_ticks;
    uint64_t idle_ticks;
    uint64_t nice_ticks;
};
struct cpu_usage get_cpu_usage(void);

#endif
