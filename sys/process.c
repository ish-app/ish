#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "sys/calls.h"
#include "sys/process.h"
#include "emu/memory.h"

__thread struct process *current;

#define MAX_PID (1 << 10) // oughta be enough
static struct pid *pids[MAX_PID + 1] = {};
pthread_mutex_t pids_lock = PTHREAD_MUTEX_INITIALIZER;

static struct pid *pid_create() {
    static int cur_pid = 1;
    pthread_mutex_lock(&pids_lock);
    while (pids[cur_pid] != NULL) {
        cur_pid++;
        if (cur_pid > MAX_PID) cur_pid = 0;
    }
    struct pid *pid = malloc(sizeof(struct pid));
    if (pid == NULL)
        return NULL;
    pid->refcnt = 1;
    pid->id = cur_pid;
    pthread_mutex_init(&pid->lock, NULL);
    pids[cur_pid] = pid;
    pthread_mutex_unlock(&pids_lock);
    return pid;
}

void pid_retain(struct pid *pid) {
    pid->refcnt++;
}
void pid_release(struct pid *pid) {
    if (--pid->refcnt == 0) {
        pthread_mutex_lock(&pids_lock);
        pids[pid->id] = NULL;
        pthread_mutex_unlock(&pids_lock);
        free(pid);
    }
}

struct pid *pid_get(dword_t id) {
    pthread_mutex_lock(&pids_lock);
    struct pid *pid = pids[id];
    if (pid != NULL)
        pid_retain(pid);
    pthread_mutex_unlock(&pids_lock);
    return pid;
}

// TODO more dry
struct process *pid_get_proc(dword_t id) {
    struct pid *pid = pid_get(id);
    if (pid == NULL) return NULL;
    lock(pid);
    struct process *proc = pid->proc;
    unlock(pid);
    pid_release(pid);
    return proc;
}

struct pgroup *pid_get_group(dword_t id) {
    struct pid *pid = pid_get(id);
    if (pid == NULL) return NULL;
    lock(pid);
    struct pgroup *group = pid->group;
    unlock(pid);
    pid_release(pid);
    return group;
}

struct session *pid_get_session(dword_t id) {
    struct pid *pid = pid_get(id);
    if (pid == NULL) return NULL;
    lock(pid);
    struct session *session = pid->session;
    unlock(pid);
    pid_release(pid);
    return session;
}

struct process *process_create() {
    struct pid *pid = pid_create();
    if (pid == NULL)
        return NULL;
    lock(pid);
    struct process *proc = malloc(sizeof(struct process));
    if (proc == NULL) {
        pid_release(pid);
        return NULL;
    }
    proc->pid = pid->id;
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
    struct pid *pid = pids[proc->pid];
    lock(pid);
    pid->proc = NULL;
    unlock(pid);
    pid_release(pid);
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
