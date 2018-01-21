#include <pthread.h>
#include <signal.h>
#include "kernel/calls.h"
#include "fs/fdtable.h"

void (*exit_hook)(int code) = NULL;

noreturn void do_exit(int status) {
    // this is the part where we release all our resources
    mem_release(current->cpu.mem);
    fdtable_release(current->files);

    // notify everyone we died
    bool init_died = current->pid == 1;
    lock(&pids_lock);
    struct task *parent = current->parent;
    unlock(&pids_lock);
    lock(&parent->exit_lock);

    current->exit_code = status;
    current->zombie = true;
    current->rusage = rusage_get_current();
    rusage_add(&parent->children_rusage, &current->rusage);

    notify(&parent->child_exit);
    unlock(&parent->exit_lock);
    notify(&current->vfork_done);

    if (init_died) {
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
    pthread_exit(NULL);
}

dword_t sys_exit(dword_t status) {
    do_exit(status << 8);
}

dword_t sys_exit_group(dword_t status) {
    do_exit(status << 8);
}

static int reap_if_zombie(struct task *task, addr_t status_addr, addr_t rusage_addr) {
    if (task->zombie) {
        if (status_addr != 0)
            if (user_put(status_addr, task->exit_code))
                return _EFAULT;
        if (rusage_addr != 0)
            if (user_put(rusage_addr, task->rusage))
                return _EFAULT;
        task_destroy(task);
        return 1;
    }
    return 0;
}

#define WNOHANG_ 1

dword_t sys_wait4(dword_t id, addr_t status_addr, dword_t options, addr_t rusage_addr) {
    STRACE("wait(%d, 0x%x, 0x%x, 0x%x)", id, status_addr, options, rusage_addr);
    lock(&current->exit_lock);

retry:
    if (id == (dword_t) -1) {
        // look for a zombie child
        struct task *task;
        list_for_each_entry(&current->children, task, siblings) {
            id = task->pid;
            if (reap_if_zombie(task, status_addr, rusage_addr))
                goto found_zombie;
        }
    } else {
        // check if this child is a zombie
        struct task *task = pid_get_proc_zombie(id);
        if (task == NULL || task->parent != current) {
            unlock(&current->exit_lock);
            return _ECHILD;
        }
        if (reap_if_zombie(task, status_addr, rusage_addr))
            goto found_zombie;
    }

    if (options & WNOHANG_) {
        unlock(&current->exit_lock);
        return _ECHILD;
    }

    // no matching zombie found, wait for one
    wait_for(&current->child_exit, &current->exit_lock);
    goto retry;

found_zombie:
    unlock(&current->exit_lock);
    return id;
}

dword_t sys_waitpid(dword_t pid, addr_t status_addr, dword_t options) {
    return sys_wait4(pid, status_addr, options, 0);
}
