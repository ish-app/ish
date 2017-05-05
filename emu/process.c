#include <stdlib.h>
#include "emu/process.h"
#include "emu/memory.h"

struct process *current;

#define MAX_PROCS (1 << 10) // oughta be enough
static struct process *procs[MAX_PROCS] = {};

struct process *process_create() {
    static int cur_pid = 0;
    while (procs[cur_pid] != NULL) {
        cur_pid++;
        if (cur_pid > MAX_PROCS) cur_pid = 0;
    }
    struct process *proc = malloc(sizeof(struct process));
    if (proc == NULL) return NULL;
    proc->cpu.pt = pt_alloc();
    procs[cur_pid] = proc;
    return proc;
}

