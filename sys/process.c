#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "sys/calls.h"
#include "sys/process.h"
#include "emu/memory.h"

__thread struct process *current;

#define MAX_PID (1 << 10) // oughta be enough
static struct pid pids[MAX_PID + 1] = {};

static bool pid_empty(struct pid *pid) {
    lock(pid);
    bool empty = pid->proc == NULL && list_empty(&pid->session) && list_empty(&pid->group);
    unlock(pid);
    return empty;
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
    lock(pid);
    struct process *proc = pid->proc;
    unlock(pid);
    return proc;
}

struct process *process_create() {
    static int cur_pid = 1;
    while (!pid_empty(&pids[cur_pid])) {
        cur_pid++;
        if (cur_pid > MAX_PID) cur_pid = 0;
    }
    struct pid *pid = &pids[cur_pid];
    lock(pid);
    pid->id = cur_pid;
    list_init(&pid->session);
    list_init(&pid->group);

    struct process *proc = malloc(sizeof(struct process));
    if (proc == NULL)
        return NULL;
    *proc = (struct process) {};
    proc->pid = pid->id;
    pid->proc = proc;
    unlock(pid);

    list_init(&proc->children);
    list_init(&proc->siblings);
    pthread_mutex_init(&proc->lock, NULL);
    pthread_cond_init(&proc->child_exit, NULL);
    pthread_cond_init(&proc->vfork_done, NULL);
    proc->has_timer = false;
    return proc;
}

void process_destroy(struct process *proc) {
    list_remove(&proc->siblings);
    struct pid *pid = pid_get(proc->pid);
    lock(pid);
    list_remove(&proc->group);
    list_remove(&proc->session);
    pid->proc = NULL;
    unlock(pid);
    // TODO free process memory
    free(proc);
}

void (*process_run_func)() = NULL;

static void *process_run(void *proc) {
    current = proc;
    if (process_run_func)
        process_run_func();
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
