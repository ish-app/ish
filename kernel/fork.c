#include "debug.h"
#include "kernel/task.h"
#include "fs/fd.h"
#include "kernel/calls.h"
#include "fs/tty.h"

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
#define IMPLEMENTED_FLAGS (CLONE_VM_|CLONE_FILES_|CLONE_FS_|CLONE_SIGHAND_|CLONE_SYSVSEM_|CLONE_VFORK_|CLONE_THREAD_|\
        CLONE_SETTLS_|CLONE_CHILD_SETTID_|CLONE_PARENT_SETTID_|CLONE_CHILD_CLEARTID_|CLONE_DETACHED_)

static struct tgroup *tgroup_copy(struct tgroup *old_group) {
    struct tgroup *group = malloc(sizeof(struct tgroup));
    *group = *old_group;
    list_init(&group->threads);
    group->tty->refcount++;
    group->has_timer = false;
    group->timer = NULL;
    group->doing_group_exit = false;
    group->children_rusage = (struct rusage_) {};
    pthread_cond_init(&group->child_exit, NULL);
    lock_init(&group->lock);
    return group;
}

static int copy_task(struct task *task, dword_t flags, addr_t stack, addr_t ptid_addr, addr_t tls_addr, addr_t ctid_addr) {
    task->vfork_done = false;
    if (stack != 0)
        task->cpu.esp = stack;

    int err;
    struct mem *mem = task->cpu.mem;
    if (flags & CLONE_VM_) {
        mem_retain(mem);
    } else {
        task->cpu.mem = mem_new();
        pt_copy_on_write(mem, 0, task->cpu.mem, 0, MEM_PAGES);
    }

    if (flags & CLONE_FILES_) {
        task->files->refcount++;
    } else {
        task->files = fdtable_copy(task->files);
        if (IS_ERR(task->files)) {
            err = PTR_ERR(task->files);
            goto fail_free_mem;
        }
    }

    err = _ENOMEM;
    if (flags & CLONE_FS_) {
        task->fs->refcount++;
    } else {
        task->fs = fs_info_copy(task->fs);
        if (task->fs == NULL)
            goto fail_free_files;
    }

    if (flags & CLONE_SIGHAND_) {
        task->sighand->refcount++;
    } else {
        task->sighand = sighand_copy(task->sighand);
        if (task->sighand == NULL)
            goto fail_free_fs;
    }

    struct tgroup *old_group = task->group;
    lock(&old_group->lock);
    if (!(flags & CLONE_THREAD_)) {
        task->group = tgroup_copy(old_group);
        task->group->leader = task;
        task->tgid = task->pid;
    }
    list_add(&task->group->threads, &task->group_links);
    unlock(&old_group->lock);

    if (flags & CLONE_SETTLS_) {
        err = task_set_thread_area(task, tls_addr);
        if (err < 0)
            goto fail_free_sighand;
    }

    err = _EFAULT;
    if (flags & CLONE_CHILD_SETTID_)
        if (user_put_task(task, ctid_addr, task->pid))
            goto fail_free_sighand;
    if (flags & CLONE_PARENT_SETTID_)
        if (user_put(ptid_addr, task->pid))
            goto fail_free_sighand;
    if (flags & CLONE_CHILD_CLEARTID_)
        task->clear_tid = ctid_addr;

    // remember to do CLONE_SYSVSEM
    return 0;

fail_free_sighand:
    sighand_release(task->sighand);
fail_free_fs:
    fs_info_release(task->fs);
fail_free_files:
    fdtable_release(task->files);
fail_free_mem:
    mem_release(task->cpu.mem);
    return err;
}

dword_t sys_clone(dword_t flags, addr_t stack, addr_t ptid, addr_t tls, addr_t ctid) {
    STRACE("clone(0x%x, 0x%x, 0x%x, 0x%x, 0x%x)", flags, stack, ptid, tls, ctid);
    if (flags & ~CSIGNAL_ & ~IMPLEMENTED_FLAGS) {
        FIXME("unimplemented clone flags 0x%x", flags & ~CSIGNAL_ & ~IMPLEMENTED_FLAGS);
        return _EINVAL;
    }
    if (flags & CLONE_SIGHAND_ && !(flags & CLONE_VM_))
        return _EINVAL;
    if (flags & CLONE_THREAD_ && !(flags & CLONE_SIGHAND_))
        return _EINVAL;

    struct task *task = task_create(current);
    if (task == NULL)
        return _ENOMEM;
    int err = copy_task(task, flags, stack, ptid, tls, ctid);
    if (err < 0) {
        task_destroy(task);
        return err;
    }
    task->cpu.eax = 0;
    task_start(task);

    if (flags & CLONE_VFORK_) {
        lock(&task->vfork_lock);
        while (!task->vfork_done)
            wait_for(&task->vfork_cond, &task->vfork_lock);
        unlock(&task->vfork_lock);
    }
    return task->pid;
}

dword_t sys_fork() {
    return sys_clone(SIGCHLD_, 0, 0, 0, 0);
}

dword_t sys_vfork() {
    return sys_clone(CLONE_VFORK_ | CLONE_VM_ | SIGCHLD_, 0, 0, 0, 0);
}

void vfork_notify(struct task *task) {
    lock(&task->vfork_lock);
    task->vfork_done = true;
    notify(&task->vfork_cond);
    unlock(&task->vfork_lock);
}
