#include <string.h>
#include "fs/stat.h"
#include "fs/proc.h"

int proc_entry_stat(struct proc_entry *entry, struct statbuf *stat) {
    memset(stat, 0, sizeof(*stat));
    stat->mode = entry->meta->mode;
    stat->inode = entry->meta->inode | entry->pid << 16;
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

bool proc_dir_read(struct proc_entry *entry, int *index, struct proc_entry *next_entry) {
    if (entry->meta->readdir)
        return entry->meta->readdir(entry, index, next_entry);

    if (entry->meta->children) {
        if (*index >= entry->meta->children_sizeof/sizeof(entry->meta->children[0]))
            return false;
        next_entry->meta = &entry->meta->children[*index];
        next_entry->pid = entry->pid;
        (*index)++;
        return true;
    }
    assert(!"read from invalid proc directory");
}

