#ifndef task_H
#define task_H

#include <pthread.h>
#include "util/list.h"
#include "emu/cpu.h"
#include "kernel/fs.h"
#include "kernel/signal.h"
#include "kernel/resource.h"
#include "util/timer.h"

struct task {
    struct cpu_state cpu; // do not access this field except on the current process
    pthread_t thread;

    dword_t pid, ppid;
    dword_t uid, gid;
    dword_t euid, egid;

    addr_t vdso;
    addr_t start_brk;
    addr_t brk;
    addr_t clear_tid;

    struct fdtable *files;
    struct fs_info *fs;

    struct sighand *sighand;
    sigset_t_ blocked;
    sigset_t_ queued; // where blocked signals go when they're sent
    sigset_t_ pending;

    struct task *parent;
    struct list children;
    struct list siblings;

    dword_t sid, pgid;
    struct list session;
    struct list group;
    struct tty *tty;

    bool has_timer;
    struct timer *timer;

    struct rlimit_ limits[RLIMIT_NLIMITS_];

    // the next two fields are protected by the exit_lock on the parent
    // process. this is because waitpid locks the parent process to wait for
    // any of its children to exit.
    dword_t exit_code;
    struct rusage_ rusage;
    bool zombie;

    struct rusage_ children_rusage;
    pthread_cond_t child_exit;
    lock_t exit_lock;

    pthread_cond_t vfork_done;
};

// current will always give the process that is currently executing
// if I have to stop using __thread, current will become a macro
extern __thread struct task *current;
#define curmem current->cpu.mem

// Creates a new process, initializes most fields from the parent. Specify
// parent as NULL to create the init process. Returns NULL if out of memory.
struct task *task_create(struct task *parent);
// Removes the process from the process table and frees it.
void task_destroy(struct task *task);

struct pid {
    dword_t id;
    struct task *task;
    struct list session;
    struct list group;
};

// these functions must be called with pids_lock
struct pid *pid_get(dword_t pid);
struct task *pid_get_task(dword_t pid);
struct task *pid_get_task_zombie(dword_t id); // don't return null if the task exists as a zombie
extern lock_t pids_lock;

#define MAX_PID (1 << 10) // oughta be enough

// When a thread is created to run a new process, this function is used.
extern void (*task_run_hook)(void);
// TODO document
void task_start(struct task *task);

extern void (*exit_hook)(int code);

#endif
