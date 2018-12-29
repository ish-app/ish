#include <stdio.h>
#include <string.h>
#include "platform/platform.h"
#include "debug.h"

static void read_proc_stat_line(const char *name, char *buf) {
    FILE *proc_stat = fopen("/proc/stat", "r");
    if (proc_stat == NULL) ERRNO_DIE("/proc/stat");
    do {
        fgets(buf, 1234, proc_stat);
    } while (strncmp(name, buf, strlen(name)) != 0 || buf[strlen(name)] != ' ');
}

struct cpu_usage get_cpu_usage() {
    struct cpu_usage usage = {};
    char buf[1234];
    read_proc_stat_line("cpu", buf);
    sscanf(buf, "cpu %llu %llu %llu %llu\n", &usage.user_ticks, &usage.system_ticks, &usage.idle_ticks, &usage.nice_ticks);
    return usage;
}
