#include <sys/sysinfo.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "platform/platform.h"
#include "debug.h"

static void read_proc_line(const char *file, const char *name, char *buf) {
    FILE *f = fopen(file, "r");
    if (f == NULL) ERRNO_DIE(file);
    do {
        fgets(buf, 1234, f);
        if (feof(f))
            die("could not find proc line %s", name);
    } while (!(strncmp(name, buf, strlen(name)) == 0 && buf[strlen(name)] == ' '));
    fclose(f);
}

struct cpu_usage get_cpu_usage() {
    struct cpu_usage usage = {};
    char buf[1234];
    read_proc_line("/proc/stat", "cpu", buf);
    sscanf(buf, "cpu %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64"\n", &usage.user_ticks, &usage.system_ticks, &usage.idle_ticks, &usage.nice_ticks);
    return usage;
}

struct mem_usage get_mem_usage() {
    struct mem_usage usage;
    char buf[1234];

    read_proc_line("/proc/meminfo", "MemTotal:", buf);
    sscanf(buf, "MemTotal: %"PRIu64" kB\n", &usage.total);
    read_proc_line("/proc/meminfo", "MemFree:", buf);
    sscanf(buf, "MemFree: %"PRIu64" kB\n", &usage.free);
    read_proc_line("/proc/meminfo", "Active:", buf);
    sscanf(buf, "Active: %"PRIu64" kB\n", &usage.active);
    read_proc_line("/proc/meminfo", "Inactive:", buf);
    sscanf(buf, "Inactive: %"PRIu64" kB\n", &usage.inactive);

    return usage;
}

struct uptime_info get_uptime() {
    struct sysinfo info;
    sysinfo(&info);
    struct uptime_info uptime = {
        .uptime_ticks = info.uptime,
        .load_1m = info.loads[0],
        .load_5m = info.loads[1],
        .load_15m = info.loads[2],
    };
    return uptime;
}
