#ifndef PROCESS_H
#define PROCESS_H

#include <pthread.h>
#include "emu/cpu.h"
#include "sys/fs.h"
#include "sys/signal.h"

struct process {
    struct cpu_state cpu;
    pthread_t thread;

    dword_t pid, ppid;
    dword_t uid, gid;
    dword_t euid, egid;

    addr_t vdso;
    addr_t start_brk;
    addr_t brk;

    path_t pwd;
    path_t root;
    struct fd *files[MAX_FD];

    struct sigaction_ sigactions[NUM_SIGS];

    struct process *parent;
    struct process *children;
    struct process *next_sibling;
    struct process *prev_sibling;
};

// current will always give the process that is currently executing
// if I have to stop using __thread, current will become a macro
extern __thread struct process *current;
#define curmem current->cpu.mem

// Creates a new process, returns NULL in case of failure
struct process *process_create(void);

// When a thread is created to run a new process, this function is used.
extern void (*run_process_func)();

#endif
