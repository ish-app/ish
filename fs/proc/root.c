#include <sys/stat.h>
#include <inttypes.h>
#include "kernel/calls.h"
#include "fs/proc.h"
#include "platform/platform.h"

static ssize_t proc_show_version(struct proc_entry *UNUSED(entry), char *buf) {
    struct uname uts;
    do_uname(&uts);
    return sprintf(buf, "%s version %s %s\n", uts.system, uts.release, uts.version);
}

static ssize_t proc_show_stat(struct proc_entry *UNUSED(entry), char *buf) {
    struct cpu_usage usage = get_cpu_usage();
    size_t n = 0;
    n += sprintf(buf + n, "cpu  %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64"\n", usage.user_ticks, usage.nice_ticks, usage.system_ticks, usage.idle_ticks);
    return n;
}

static void show_kb(char *buf, size_t *n, const char *name, uint64_t value) {
    *n += sprintf(buf + *n, "%s%8"PRIu64" kB\n", name, value / 1000);
}

static ssize_t proc_show_meminfo(struct proc_entry *UNUSED(entry), char *buf) {
    struct mem_usage usage = get_mem_usage();
    size_t n = 0;
    show_kb(buf, &n, "MemTotal:       ", usage.total);
    show_kb(buf, &n, "MemFree:        ", usage.free);
    show_kb(buf, &n, "MemShared:      ", usage.free);
    // a bunch of crap busybox top needs to see or else it gets stack garbage
    show_kb(buf, &n, "Shmem:          ", 0);
    show_kb(buf, &n, "Buffers:        ", 0);
    show_kb(buf, &n, "Cached:         ", 0);
    show_kb(buf, &n, "SwapTotal:      ", 0);
    show_kb(buf, &n, "SwapFree:       ", 0);
    show_kb(buf, &n, "Dirty:          ", 0);
    show_kb(buf, &n, "Writeback:      ", 0);
    show_kb(buf, &n, "AnonPages:      ", 0);
    show_kb(buf, &n, "Mapped:         ", 0);
    show_kb(buf, &n, "Slab:           ", 0);
    return n;
}

static ssize_t proc_show_uptime(struct proc_entry *UNUSED(entry), char *buf) {
    struct uptime_info uptime_info = get_uptime();
    unsigned uptime = uptime_info.uptime_ticks;
    size_t n = 0;
    n += sprintf(buf + n, "%u.%u %u.%u\n", uptime / 100, uptime % 100, uptime / 100, uptime % 100);
    return n;
}

static int proc_readlink_self(struct proc_entry *UNUSED(entry), char *buf) {
    sprintf(buf, "%d/", current->pid);
    return 0;
}

// in no particular order
struct proc_dir_entry proc_root_entries[] = {
    {"version", .show = proc_show_version},
    {"stat", .show = proc_show_stat},
    {"meminfo", .show = proc_show_meminfo},
    {"uptime", .show = proc_show_uptime},
    {"self", S_IFLNK, .readlink = proc_readlink_self},
};
#define PROC_ROOT_LEN sizeof(proc_root_entries)/sizeof(proc_root_entries[0])

static bool proc_root_readdir(struct proc_entry *UNUSED(entry), unsigned long *index, struct proc_entry *next_entry) {
    if (*index < PROC_ROOT_LEN) {
        *next_entry = (struct proc_entry) {&proc_root_entries[*index], 0, 0};
        (*index)++;
        return true;
    }

    pid_t_ pid = *index - PROC_ROOT_LEN;
    if (pid <= MAX_PID) {
        lock(&pids_lock);
        do {
            pid++;
        } while (pid <= MAX_PID && pid_get_task(pid) == NULL);
        unlock(&pids_lock);
        if (pid > MAX_PID)
            return false;
        assert(pid_get_task(pid) != NULL);
        *next_entry = (struct proc_entry) {&proc_pid, .pid = pid};
        *index = pid + PROC_ROOT_LEN;
        return true;
    }

    return false;
}

struct proc_dir_entry proc_root = {NULL, S_IFDIR, .readdir = proc_root_readdir};
