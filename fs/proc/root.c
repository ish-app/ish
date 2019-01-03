#include <sys/stat.h>
#include "kernel/calls.h"
#include "fs/proc.h"
#include "platform/platform.h"

static ssize_t proc_show_version(struct proc_entry *entry, char *buf) {
    struct uname uts;
    do_uname(&uts);
    return sprintf(buf, "%s version %s %s\n", uts.system, uts.release, uts.version);
}

static ssize_t proc_show_stat(struct proc_entry *entry, char *buf) {
    struct cpu_usage usage = get_cpu_usage();
    size_t n = 0;
    n += sprintf(buf + n, "cpu  %llu %llu %llu %llu\n", usage.user_ticks, usage.nice_ticks, usage.system_ticks, usage.idle_ticks);
    return n;
}

static int proc_readlink_self(struct proc_entry *entry, char *buf) {
    sprintf(buf, "%d/", current->pid);
    return 0;
}

// in no particular order
struct proc_dir_entry proc_root_entries[] = {
    {2, "version", .show = proc_show_version},
    {3, "stat", .show = proc_show_stat},
    {10, "self", S_IFLNK, .readlink = proc_readlink_self},
};
#define PROC_ROOT_LEN sizeof(proc_root_entries)/sizeof(proc_root_entries[0])

static bool proc_root_readdir(struct proc_entry *entry, int *index, struct proc_entry *next_entry) {
    if (*index < PROC_ROOT_LEN) {
        *next_entry = (struct proc_entry) {&proc_root_entries[*index]};
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

struct proc_dir_entry proc_root = {1, NULL, S_IFDIR, .readdir = proc_root_readdir};
