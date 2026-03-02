#include <sys/stat.h>
#include <string.h>
#include "fs/stat.h"
#include "fs/proc.h"
#include "kernel/task.h"

mode_t_ proc_entry_mode(struct proc_entry *entry) {
    mode_t_ mode = entry->meta->mode;
    if ((mode & S_IFMT) == 0)
        mode |= S_IFREG;
    if ((mode & 0777) == 0) {
        if (S_ISREG(mode))
            mode |= 0444;
        else if (S_ISDIR(mode))
            mode |= 0555;
        else if (S_ISLNK(mode))
            mode |= 0777;
    }
    return mode;
}

int proc_entry_stat(struct proc_entry *entry, struct statbuf *stat) {
    memset(stat, 0, sizeof(*stat));
    stat->mode = proc_entry_mode(entry);

    lock(&pids_lock);
    struct task *task = pid_get_task(entry->pid);

    if (task != NULL) {
        stat->uid = task->uid;
        stat->gid = task->gid;
    } // else the memset above will have initialized memory to zero, which is the root uid/gid

    unlock(&pids_lock);

    stat->inode = entry->meta->inode | entry->pid << 16 | (uint64_t) entry->fd << 48;
    return 0;
}

void proc_entry_getname(struct proc_entry *entry, char *buf) {
    if (entry->meta->getname)
        entry->meta->getname(entry, buf);
    else if (entry->meta->name)
        strcpy(buf, entry->meta->name);
    else
        assert(!"missing name in proc entry");
}

bool proc_dir_read(struct proc_entry *entry, unsigned long *index, struct proc_entry *next_entry) {
    if (entry->meta->readdir)
        return entry->meta->readdir(entry, index, next_entry);

    if (entry->meta->children) {
        if (*index >= entry->meta->children->count)
            return false;
        next_entry->meta = &entry->meta->children->entries[*index];
        next_entry->pid = entry->pid;
        (*index)++;
        return true;
    }
    assert(!"read from invalid proc directory");
}

void free_string_array(char **array) {
    for (int i = 0; array[i] != NULL; i++)
        free(array[i]);
    free(array);
}

void proc_entry_cleanup(struct proc_entry *entry) {
    if (entry->name != NULL)
        free(entry->name);
    if (entry->child_names != NULL)
        free_string_array(entry->child_names);
    *entry = (struct proc_entry) {0};
}
