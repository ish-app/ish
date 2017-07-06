#ifndef PROCESS_H
#define PROCESS_H

#include "emu/cpu.h"
#include "sys/fs.h"
#include "sys/signal.h"

struct process {
    struct cpu_state cpu;

    dword_t pid;
    dword_t uid, gid;

    addr_t vdso;
    addr_t start_brk;
    addr_t brk;

    path_t pwd;
    path_t root;
    struct fd *files[MAX_FD];

    struct sigaction_ sigactions[NUM_SIGS];
};

// current will always give the process that is currently executing
// if I have to stop using __thread, current will become a macro
extern struct process *current;
#define curmem current->cpu.mem

// Creates a new process, returns NULL in case of failure
struct process *process_create(void);

#endif
