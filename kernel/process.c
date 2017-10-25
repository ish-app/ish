#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/calls.h"
#include "kernel/process.h"
#include "emu/memory.h"

__thread struct process *current;

static struct pid pids[MAX_PID + 1] = {};
pthread_mutex_t pids_lock = PTHREAD_MUTEX_INITIALIZER;

static bool pid_empty(struct pid *pid) {
    return pid->proc == NULL && list_empty(&pid->session) && list_empty(&pid->group);
}

struct pid *pid_get(dword_t id) {
    struct pid *pid = &pids[id];
    if (pid_empty(pid))
        return NULL;
    return pid;
}

struct process *pid_get_proc(dword_t id) {
    struct pid *pid = pid_get(id);
    if (pid == NULL) return NULL;
    struct process *proc = pid->proc;
    return proc;
}

struct process *process_create() {
    big_lock(pids);
    static int cur_pid = 1;
    while (!pid_empty(&pids[cur_pid])) {
        cur_pid++;
        if (cur_pid > MAX_PID) cur_pid = 0;
    }
    struct pid *pid = &pids[cur_pid];
    pid->id = cur_pid;
    list_init(&pid->session);
    list_init(&pid->group);

    struct process *proc = malloc(sizeof(struct process));
    if (proc == NULL)
        return NULL;
    *proc = (struct process) {};
    proc->pid = pid->id;
    pid->proc = proc;
    big_unlock(pids);

    list_init(&proc->children);
    list_init(&proc->siblings);
    lock_init(proc);
    pthread_cond_init(&proc->child_exit, NULL);
    pthread_cond_init(&proc->vfork_done, NULL);
    proc->has_timer = false;
    return proc;
}

void process_destroy(struct process *proc) {
    list_remove(&proc->siblings);
    big_lock(pids);
    struct pid *pid = pid_get(proc->pid);
    list_remove(&proc->group);
    list_remove(&proc->session);
    pid->proc = NULL;
    big_unlock(pids);
    mem_release(proc->cpu.mem);
    free(proc);
}

void (*process_run_hook)() = NULL;

static void *process_run(void *proc) {
    current = proc;
    if (process_run_hook)
        process_run_hook();
    else
        cpu_run(&current->cpu);
    assert(false);
}

void start_thread(struct process *proc) {
    if (pthread_create(&proc->thread, NULL, process_run, proc) < 0)
        abort();
}

// dumps out a tree of the processes, useful for running from a debugger
/* static void pstree( */
