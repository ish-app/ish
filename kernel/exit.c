#include <pthread.h>
#include <signal.h>
#include "kernel/calls.h"
#include "fs/fd.h"

static void halt_system(int status);

static void exit_tgroup(struct task *task) {
    struct tgroup *group = task->group;
    lock(&group->lock);
    list_remove(&task->group_links);
    if (list_empty(&group->threads))
        free(group);
    else
        unlock(&group->lock);
}

noreturn void do_exit(int status) {
    bool was_leader = task_is_leader(current);

    // release all our resources
    mem_release(current->cpu.mem);
    fdtable_release(current->files);
    fs_info_release(current->fs);
    sighand_release(current->sighand);
    exit_tgroup(current);
    vfork_notify(current);

    // save things that our parent might be interested in
    current->exit_code = status;
    struct rusage_ rusage = rusage_get_current();
    lock(&current->group->lock);
    rusage_add(&current->group->rusage, &rusage);
    unlock(&current->group->lock);

    // notify everyone we died
    lock(&pids_lock);
    struct task *parent = current->parent;
    unlock(&pids_lock);
    lock(&parent->group->lock);
    if (was_leader) {
        current->zombie = true;
        notify(&parent->group->child_exit);
    }
    unlock(&parent->group->lock);

    if (current->pid == 1)
        halt_system(status);
    pthread_exit(NULL);
}

void (*exit_hook)(int code) = NULL;
static void halt_system(int status) {
    // brutally murder everything
    // which will leave everything in an inconsistent state. I will solve this problem later.
    lock(&pids_lock);
    for (int i = 2; i < MAX_PID; i++) {
        struct task *task = pid_get_task(i);
        if (task != NULL)
            pthread_kill(task->thread, SIGKILL);
    }
    unlock(&pids_lock);

    // unmount all filesystems
    struct mount *mount = mounts;
    while (mount) {
        if (mount->fs->umount)
            mount->fs->umount(mount);
        mount = mount->next;
    }

    if (exit_hook != NULL)
        exit_hook(status);
}

dword_t sys_exit(dword_t status) {
    do_exit(status << 8);
}

dword_t sys_exit_group(dword_t status) {
    TODO("exit_group");
    /* do_exit(status << 8); */
}

static int reap_if_zombie(struct task *task, addr_t status_addr, addr_t rusage_addr) {
    assert(task_is_leader(task));
    if (!task->zombie) 
        return 0;

    // account for task's resource usage
    lock(&task->group->lock);
    struct rusage_ rusage = task->group->rusage;
    rusage_add(&current->group->children_rusage, &rusage);
    unlock(&task->group->lock);

    if (status_addr != 0)
        if (user_put(status_addr, task->exit_code))
            return _EFAULT;
    if (rusage_addr != 0)
        if (user_put(rusage_addr, rusage))
            return _EFAULT;

    task_destroy(task);
    return 1;
}

#define WNOHANG_ 1

dword_t sys_wait4(dword_t id, addr_t status_addr, dword_t options, addr_t rusage_addr) {
    STRACE("wait(%d, 0x%x, 0x%x, 0x%x)", id, status_addr, options, rusage_addr);
    lock(&current->group->lock);

retry:
    if (id == (dword_t) -1) {
        // look for a zombie child
        struct task *task;
        lock(&pids_lock);
        list_for_each_entry(&current->children, task, siblings) {
            id = task->pid;
            if (reap_if_zombie(task, status_addr, rusage_addr)) {
                unlock(&pids_lock);
                goto found_zombie;
            }
        }
        unlock(&pids_lock);
    } else {
        // check if this child is a zombie
        struct task *task = pid_get_task_zombie(id);
        if (task == NULL || task->parent != current) {
            unlock(&current->group->lock);
            return _ECHILD;
        }
        if (reap_if_zombie(task, status_addr, rusage_addr))
            goto found_zombie;
    }

    if (options & WNOHANG_) {
        unlock(&current->group->lock);
        return _ECHILD;
    }

    // no matching zombie found, wait for one
    wait_for(&current->group->child_exit, &current->group->lock);
    goto retry;

found_zombie:
    unlock(&current->group->lock);
    return id;
}

dword_t sys_waitpid(dword_t pid, addr_t status_addr, dword_t options) {
    return sys_wait4(pid, status_addr, options, 0);
}
