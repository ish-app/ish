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

#define CSIGNAL_ 0x000000ff
#define CLONE_VM_ 0x00000100
#define CLONE_FS_ 0x00000200
#define CLONE_FILES_ 0x00000400
#define CLONE_SIGHAND_ 0x00000800
#define CLONE_PTRACE_ 0x00002000
#define CLONE_VFORK_ 0x00004000
#define CLONE_PARENT_ 0x00008000
#define CLONE_THREAD_ 0x00010000
#define CLONE_NEWNS_ 0x00020000
#define CLONE_SYSVSEM_ 0x00040000
#define CLONE_SETTLS_ 0x00080000
#define CLONE_PARENT_SETTID_ 0x00100000
#define CLONE_CHILD_CLEARTID_ 0x00200000
#define CLONE_DETACHED_ 0x00400000
#define CLONE_UNTRACED_ 0x00800000
#define CLONE_CHILD_SETTID_ 0x01000000
#define CLONE_NEWCGROUP_ 0x02000000
#define CLONE_NEWUTS_ 0x04000000
#define CLONE_NEWIPC_ 0x08000000
#define CLONE_NEWUSER_ 0x10000000
#define CLONE_NEWPID_ 0x20000000
#define CLONE_NEWNET_ 0x40000000
#define CLONE_IO_ 0x80000000

static int copy_memory(struct process *proc, int flags) {
    if (flags & CLONE_VM_)
        return 0;
    struct mem old_mem = proc->cpu.mem;
    struct mem *new_mem = &proc->cpu.mem;
    mem_init(new_mem);
    pt_copy_on_write(&old_mem, 0, new_mem, 0, PT_SIZE);
    return 0;
}

// eax = syscall number
// ebx = flags
// ecx = stack
// edx, esi, edi = unimplemented garbage
static int dup_process(int flags, addr_t stack, addr_t ptid, addr_t tls, addr_t ctid, struct process **p) {
    if (ptid != 0 || tls != 0) {
        FIXME("clone with ptid or ts not null");
        return _EINVAL;
    }
    if (flags & CLONE_CHILD_CLEARTID_)
        FIXME("clone(CLONE_CHILD_CLEARTID)");
    if ((flags & CSIGNAL_) != SIGCHLD_) {
        FIXME("clone non sigchld");
        return _EINVAL;
    }

    if (stack == 0)
        stack = current->cpu.esp;

    struct process *proc = process_create();
    if (proc == NULL)
        return _ENOMEM;
    dword_t pid = proc->pid;
    *proc = *current;
    proc->pid = pid;
    proc->ppid = current->pid;

    int err = 0;
    if ((err = copy_memory(proc, flags)) < 0)
        goto fail_free_proc;

    proc->parent = current;
    list_add(&current->children, &proc->siblings);

    proc->cpu.eax = 0;
    if (flags & CLONE_CHILD_SETTID_)
        user_put_proc(proc, ctid, proc->pid);
    start_thread(proc);

    *p = proc;
    return 0;

fail_free_proc:
    free(proc);
    return err;
}

dword_t sys_clone(dword_t flags, addr_t stack, addr_t ptid, addr_t tls, addr_t ctid) {
    struct process *proc;
    int err = dup_process(flags, stack, ptid, tls, ctid, &proc);
    if (err < 0)
        return err;
    return proc->pid;
}
