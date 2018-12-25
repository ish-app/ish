#include <sys/stat.h>
#include "kernel/calls.h"
#include "fs/proc.h"

static size_t proc_show_version(struct proc_entry *entry, char *buf) {
    struct uname uts;
    do_uname(&uts);
    return sprintf(buf, "%s version %s %s\n", uts.system, uts.release, uts.version);
}

struct proc_dir_entry proc_root_entries[] = {
    {2, "version", S_IFREG | 0444, .show = proc_show_version},
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
        printk("task %d = %p", pid, pid_get_task(pid));
        assert(pid_get_task(pid) != NULL);
        *next_entry = (struct proc_entry) {&proc_pid, .pid = pid};
        *index = pid + PROC_ROOT_LEN;
        return true;
    }

    return false;
}

struct proc_dir_entry proc_root = {1, NULL, S_IFDIR | 0555, .readdir = proc_root_readdir};

static void proc_pid_getname(struct proc_entry *entry, char *buf) {
    sprintf(buf, "%d", entry->pid);
}
static bool proc_pid_readdir(struct proc_entry *entry, int *index, struct proc_entry *next_entry) {
    return false;
}
struct proc_dir_entry proc_pid = {1, NULL, S_IFDIR | 0555, .readdir = proc_pid_readdir, .getname = proc_pid_getname};
