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
struct cpu_usage get_total_cpu_usage(void);
int get_per_cpu_usage(struct cpu_usage** cpus_usage);

struct mem_usage {
    uint64_t total;
    uint64_t free;
    uint64_t active;
    uint64_t inactive;
};
struct mem_usage get_mem_usage(void);

struct uptime_info {
    uint64_t uptime_ticks;
    uint64_t load_1m, load_5m, load_15m;
};
struct uptime_info get_uptime(void);

int get_cpu_count(void);

#endif
