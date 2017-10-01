#include "debug.h"
#include "sys/process.h"
#include "sys/calls.h"

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

static int init_process(struct process *proc, dword_t flags, addr_t ctid_addr) {
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
        if (user_put_proc(proc, ctid_addr, proc->pid)) {
            err = _EFAULT;
            goto fail_free_proc;
        }
    start_thread(proc);

    if (flags & CLONE_VFORK_)
        wait_for(proc, vfork_done);

    return 0;

fail_free_proc:
    free(proc);
    return err;
}

// eax = syscall number
// ebx = flags
// ecx = stack
// edx, esi, edi = unimplemented garbage
dword_t sys_clone(dword_t flags, addr_t stack, addr_t ptid, addr_t tls, addr_t ctid) {
    if (ptid != 0 || tls != 0) {
        FIXME("clone with ptid or ts not null");
        return _EINVAL;
    }
    if ((flags & CSIGNAL_) != SIGCHLD_) {
        FIXME("clone non sigchld");
        return _EINVAL;
    }

    if (stack == 0)
        stack = current->cpu.esp;

    struct process *proc = process_create();
    if (proc == NULL)
        return _ENOMEM;
    lock(proc);
    int err = init_process(proc, flags, ctid);
    unlock(proc);
    if (err < 0) {
        process_destroy(proc);
        return err;
    }
    return proc->pid;
}

dword_t sys_fork() {
    return sys_clone(SIGCHLD_, current->cpu.esp, 0, 0, 0);
}

dword_t sys_vfork() {
    return sys_clone(CLONE_VFORK_ | CLONE_VM_ | SIGCHLD_, current->cpu.esp, 0, 0, 0);
}
