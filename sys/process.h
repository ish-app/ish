#ifndef PROCESS_H
#define PROCESS_H

#include <pthread.h>
#include "util/list.h"
#include "emu/cpu.h"
#include "sys/fs.h"
#include "sys/signal.h"

struct proc_group;
struct session;

struct process {
    struct cpu_state cpu;
    pthread_t thread;

    dword_t pid, ppid;
    dword_t uid, gid;
    dword_t euid, egid;

    addr_t vdso;
    addr_t start_brk;
    addr_t brk;

    char *pwd;
    char *root;
    struct fd *files[MAX_FD];

    struct sigaction_ sigactions[NUM_SIGS];
    sigset_t_ blocked;
    sigset_t_ queued; // where blocked signals go when they're sent
    sigset_t_ pending;

    struct process *parent;
    struct list children;
    struct list siblings;

    struct proc_group *group;
    struct list group_procs;

    bool has_timer;
    timer_t timer;

    // the next two fields are protected by the lock on the parent process, not
    // the lock on the process. this is because waitpid locks the parent
    // process to wait for any of its children to exit.
    dword_t exit_code;
    bool zombie;
    pthread_cond_t child_exit;

    pthread_cond_t vfork_done;

    pthread_mutex_t lock;
};

// current will always give the process that is currently executing
// if I have to stop using __thread, current will become a macro
extern __thread struct process *current;
#define curmem current->cpu.mem

// Creates a new process, returns NULL in case of failure
struct process *process_create(void);
// Removes the process from the process table and frees it.
void process_destroy(struct process *proc);

struct pid {
    unsigned refcnt;
    dword_t id;
    struct process *proc;
    struct proc_group *group;
    struct session *session;
    pthread_mutex_t lock;
};

// Returns the process with the given PID, or NULL if it doesn't exist.
struct pid *pid_get(dword_t pid);

struct proc_group {
    struct list procs;
    struct session *session;
    struct list session_groups;
    pthread_mutex_t lock;
};

struct session {
    struct list groups;
    pthread_mutex_t lock;
};

// When a thread is created to run a new process, this function is used.
extern void (*run_process_func)();
// TODO document
void start_thread(struct process *proc);

#endif
