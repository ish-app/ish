#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "sys/calls.h"
#include "sys/process.h"
#include "emu/memory.h"

__thread struct process *current;

#define MAX_PROCS (1 << 10) // oughta be enough
static struct process *procs[MAX_PROCS] = {};

static int next_pid() {
    static int cur_pid = 1;
    while (procs[cur_pid] != NULL) {
        cur_pid++;
        if (cur_pid > MAX_PROCS) cur_pid = 0;
    }
    return cur_pid;
}

struct process *process_create() {
    struct process *proc = malloc(sizeof(struct process));
    if (proc == NULL) return NULL;
    proc->pid = next_pid();
    list_init(&proc->children);
    list_init(&proc->siblings);
    pthread_mutex_init(&proc->lock, NULL);
    pthread_cond_init(&proc->child_exit, NULL);
    pthread_cond_init(&proc->vfork_done, NULL);
    proc->has_timer = false;
    procs[proc->pid] = proc;
    return proc;
}

void process_destroy(struct process *proc) {
    list_remove(&proc->siblings);
    procs[proc->pid] = NULL;
    free(proc);
}

struct process *process_for_pid(dword_t pid) {
    return procs[pid];
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
