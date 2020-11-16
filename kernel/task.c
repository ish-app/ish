#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/calls.h"
#include "kernel/task.h"
#include "emu/memory.h"
#include "emu/tlb.h"

__thread struct task *current;

static dword_t last_allocated_pid = 0;
static struct pid pids[MAX_PID + 1] = {};
lock_t pids_lock = LOCK_INITIALIZER;
struct list alive_pids_list;

static bool pid_empty(struct pid *pid) {
    return pid->task == NULL && list_empty(&pid->session) && list_empty(&pid->pgroup);
}

struct pid *pid_get(dword_t id) {
    if (id > sizeof(pids)/sizeof(pids[0]))
        return NULL;
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

struct pid *pid_get_last_allocated() {
    if (!last_allocated_pid) {
        return NULL;
    }
    return pid_get(last_allocated_pid);
}

dword_t get_count_of_blocked_tasks() {
    lock(&pids_lock);
    dword_t res = 0;
    struct pid *pid_entry;
    list_for_each_entry(&alive_pids_list, pid_entry, alive) {
        if (pid_entry->task->io_block) {
            res++;
        }
    }
    unlock(&pids_lock);
    return res;
}

dword_t get_count_of_alive_tasks() {
    lock(&pids_lock);
    dword_t res = 0;
    struct list *item;
    list_for_each(&alive_pids_list, item) {
        res++;
    }
    unlock(&pids_lock);
    return res;
}

struct task *task_create_(struct task *parent) {
    lock(&pids_lock);
    do {
        last_allocated_pid++;
        if (last_allocated_pid > MAX_PID) last_allocated_pid = 1;
    } while (!pid_empty(&pids[last_allocated_pid]));
    struct pid *pid = &pids[last_allocated_pid];
    pid->id = last_allocated_pid;
    list_init(&pid->alive);
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
    list_add(&alive_pids_list, &pid->alive);

    list_init(&task->children);
    list_init(&task->siblings);
    if (parent != NULL) {
        task->parent = parent;
        list_add(&parent->children, &task->siblings);
    }
    unlock(&pids_lock);

    task->pending = 0;
    list_init(&task->queue);
    task->clear_tid = 0;
    task->robust_list = 0;
    task->did_exec = false;
    lock_init(&task->general_lock);

    task->sockrestart = (struct task_sockrestart) {};
    list_init(&task->sockrestart.listen);

    task->waiting_cond = NULL;
    task->waiting_lock = NULL;
    lock_init(&task->waiting_cond_lock);
    cond_init(&task->pause);

    lock_init(&task->ptrace.lock);
    cond_init(&task->ptrace.cond);
    return task;
}

void task_destroy(struct task *task) {
    list_remove(&task->siblings);
    struct pid *pid = pid_get(task->pid);
    pid->task = NULL;
    list_remove(&pid->alive);
    free(task);
}

void task_run_current() {
    struct cpu_state *cpu = &current->cpu;
    struct tlb tlb;
    tlb_refresh(&tlb, current->mem);
    while (true) {
        int interrupt = cpu_run_to_interrupt(cpu, &tlb);
        handle_interrupt(interrupt);
    }
}

static void *task_thread(void *task) {
    current = task;
    update_thread_name();
    task_run_current();
    die("task_thread returned"); // above function call should never return
}

static pthread_attr_t task_thread_attr;
__attribute__((constructor)) static void create_attr() {
    pthread_attr_init(&task_thread_attr);
    pthread_attr_setdetachstate(&task_thread_attr, PTHREAD_CREATE_DETACHED);
}

void task_start(struct task *task) {
    if (pthread_create(&task->thread, &task_thread_attr, task_thread, task) < 0)
        die("could not create thread");
}

int_t sys_sched_yield() {
    STRACE("sched_yield()");
    sched_yield();
    return 0;
}

void update_thread_name() {
    char name[16]; // As long as Linux will let us make this
    snprintf(name, sizeof(name), "-%d", current->pid);
    size_t pid_width = strlen(name);
    size_t name_width = snprintf(name, sizeof(name), "%s", current->comm);
    sprintf(name + (name_width < sizeof(name) - 1 - pid_width ? name_width : sizeof(name) - 1 - pid_width), "-%d", current->pid);
#if __APPLE__
    pthread_setname_np(name);
#else
    pthread_setname_np(pthread_self(), name);
#endif
}
