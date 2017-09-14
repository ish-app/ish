#ifndef PROCESS_H
#define PROCESS_H

#include <pthread.h>
#include <stdatomic.h>
#include "util/list.h"
#include "emu/cpu.h"
#include "sys/fs.h"
#include "sys/signal.h"

struct process {
    struct cpu_state cpu; // do not access this field except on the current process
    pthread_t thread;

    dword_t pid, ppid;
    dword_t uid, gid;
    dword_t euid, egid;

    addr_t vdso;
    addr_t start_brk;
    addr_t brk;

    struct fd *pwd;
    struct fd *root;
    struct fd *files[MAX_FD];
    mode_t_ umask;

    struct sigaction_ sigactions[NUM_SIGS];
    sigset_t_ blocked;
    sigset_t_ queued; // where blocked signals go when they're sent
    sigset_t_ pending;

    struct process *parent;
    struct list children;
    struct list siblings;

    dword_t sid, pgid;
    struct list session;
    struct list group;
    struct tty *tty;

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
    dword_t id; // immutable, no lock needed
    struct process *proc;
    struct list session;
    struct list group;
    pthread_mutex_t lock;
};

struct pid *pid_get(dword_t pid);
struct process *pid_get_proc(dword_t pid);

// When a thread is created to run a new process, this function is used.
extern void (*run_process_func)();
// TODO document
void start_thread(struct process *proc);

#endif
