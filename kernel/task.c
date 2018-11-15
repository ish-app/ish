#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/calls.h"
#include "kernel/task.h"
#include "emu/memory.h"

__thread struct task *current;

static struct pid pids[MAX_PID + 1] = {};
lock_t pids_lock = LOCK_INITIALIZER;

static bool pid_empty(struct pid *pid) {
    return pid->task == NULL && list_empty(&pid->session) && list_empty(&pid->pgroup);
}

struct pid *pid_get(dword_t id) {
    struct pid *pid = &pids[id];
    if (pid_empty(pid))
        return NULL;
    return pid;
}

struct task *pid_get_task_zombie(dword_t id) {
    struct pid *pid = pid_get(id);
    if (pid == NULL)
        return NULL;
    struct task *task = pid->task;
    return task;
}

struct task *pid_get_task(dword_t id) {
    struct task *task = pid_get_task_zombie(id);
    if (task != NULL && task->zombie)
        return NULL;
    return task;
}

struct task *task_create_(struct task *parent) {
    lock(&pids_lock);
    static int cur_pid = 1;
    while (!pid_empty(&pids[cur_pid])) {
        cur_pid++;
        if (cur_pid > MAX_PID) cur_pid = 0;
    }
    struct pid *pid = &pids[cur_pid];
    pid->id = cur_pid;
    list_init(&pid->session);
    list_init(&pid->pgroup);

    struct task *task = malloc(sizeof(struct task));
    if (task == NULL)
        return NULL;
    *task = (struct task) {};
    if (parent != NULL)
        *task = *parent;
    task->pid = pid->id;
    pid->task = task;
    unlock(&pids_lock);

    task->did_exec = false;
    list_init(&task->children);
    list_init(&task->siblings);
    if (parent != NULL) {
        task->parent = parent;
        list_add(&parent->children, &task->siblings);
        list_add(&parent->pgroup, &task->pgroup);
        list_add(&parent->session, &task->session);
    }

    lock_init(&task->vfork_lock);
    cond_init(&task->vfork_cond);
    task->waiting_cond = NULL;
    task->waiting_lock = NULL;
    lock_init(&task->waiting_cond_lock);
    return task;
}

void task_destroy(struct task *task) {
    list_remove(&task->siblings);
    list_remove(&task->pgroup);
    list_remove(&task->session);
    pid_get(task->pid)->task = NULL;
    cond_destroy(&task->vfork_cond);
    free(task);
}

void (*task_run_hook)(void) = NULL;

static void *task_run(void *task) {
    current = task;
    if (task_run_hook)
        task_run_hook();
    else
        cpu_run(&current->cpu);
    abort(); // above function call should never return
}

static pthread_attr_t task_thread_attr;
__attribute__((constructor)) static void create_attr() {
    pthread_attr_init(&task_thread_attr);
    pthread_attr_setdetachstate(&task_thread_attr, PTHREAD_CREATE_DETACHED);
}

void task_start(struct task *task) {
    if (pthread_create(&task->thread, &task_thread_attr, task_run, task) < 0)
        abort();
}
