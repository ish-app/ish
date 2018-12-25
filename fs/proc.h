#ifndef FS_PROC_H
#define FS_PROC_H

#include "fs/stat.h"
#include "misc.h"

struct proc_entry {
    struct proc_dir_entry *meta;
    pid_t_ pid;
};

struct proc_dir_entry {
    int inode;
    const char *name;
    mode_t_ mode;

    // file with dynamic name
    void (*getname)(struct proc_entry *entry, char *buf);

    // file with custom show data function
    // not worrying about buffer overflows for now
    ssize_t (*show)(struct proc_entry *entry, char *buf);

    // directory with static list
    struct proc_dir_entry *children;
    size_t children_sizeof;

    // directory with dynamic contents
    bool (*readdir)(struct proc_entry *entry, int *index, struct proc_entry *next_entry);
};

extern struct proc_dir_entry proc_root;
extern struct proc_dir_entry proc_pid;

void proc_entry_getname(struct proc_entry *entry, char *buf);
int proc_entry_stat(struct proc_entry *entry, struct statbuf *stat);

bool proc_dir_read(struct proc_entry *entry, int *index, struct proc_entry *next_entry);

#endif
