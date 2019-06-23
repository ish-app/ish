#include <pthread.h>
#include <signal.h>
#include "kernel/calls.h"
#include "kernel/mm.h"
#include "kernel/futex.h"
#include "fs/fd.h"
#include "fs/tty.h"

static void halt_system(void);

static bool exit_tgroup(struct task *task) {
    struct tgroup *group = task->group;
    list_remove(&task->group_links);
    bool group_dead = list_empty(&group->threads);
    if (group_dead) {
        // don't need to lock the group since the only pointers it come from:
        // - other threads current->group, but the other threads have accessed it
        // - locking pids_lock first, which do_exit did
        if (group->timer)
            timer_free(group->timer);
        task_leave_session(task);
        list_remove(&group->pgroup);
    }
    return group_dead;
}

void (*exit_hook)(struct task *task, int code) = NULL;

static struct task *find_new_parent(struct task *task) {
    struct task *new_parent;
    list_for_each_entry(&task->group->threads, new_parent, group_links) {
        if (!new_parent->exiting)
            return new_parent;
    }
    return pid_get_task(1);
}

noreturn void do_exit(int status) {
    // has to happen before mm_release
    addr_t clear_tid = current->clear_tid;
    if (clear_tid) {
        pid_t_ zero = 0;
        if (user_put(clear_tid, zero) == 0)
            futex_wake(clear_tid, 1);
    }

    // release all our resources
    mm_release(current->mm);
    fdtable_release(current->files);
    fs_info_release(current->fs);
    // sighand must be released below so it can be protected by pids_lock
    // since it can be accessed by other threads

    // save things that our parent might be interested in
    current->exit_code = status; // FIXME locking
    struct rusage_ rusage = rusage_get_current();
    lock(&current->group->lock);
    rusage_add(&current->group->rusage, &rusage);
    struct rusage_ group_rusage = current->group->rusage;
    unlock(&current->group->lock);

    // the actual freeing needs pids_lock
    lock(&pids_lock);
    current->exiting = true;
    // release the sighand
    sighand_release(current->sighand);
    struct task *leader = current->group->leader;

    // reparent children
    struct task *new_parent = find_new_parent(current);
    struct task *child, *tmp;
    list_for_each_entry_safe(&current->children, child, tmp, siblings) {
        child->parent = new_parent;
        list_remove(&child->siblings);
        list_add(&new_parent->children, &child->siblings);
    }

    if (exit_tgroup(current)) {
        // notify parent that we died
        struct task *parent = leader->parent;
        if (parent == NULL) {
            // init died
            halt_system();
        } else {
            leader->zombie = true;
            notify(&parent->group->child_exit);
            struct siginfo_ info = {
                .code = SI_KERNEL_,
                .child.pid = current->pid,
                .child.uid = current->uid,
                .child.status = current->exit_code,
                .child.utime = clock_from_timeval(group_rusage.utime),
                .child.stime = clock_from_timeval(group_rusage.stime),
            };
            if (leader->exit_signal != 0)
                send_signal(parent, leader->exit_signal, info);
        }

        if (exit_hook != NULL)
            exit_hook(current, status);
    }

    vfork_notify(current);
    if (current != leader)
        task_destroy(current);
    unlock(&pids_lock);

    pthread_exit(NULL);
}

noreturn void do_exit_group(int status) {
    struct tgroup *group = current->group;
    lock(&group->lock);
    if (!group->doing_group_exit) {
        group->doing_group_exit = true;
        group->group_exit_code = status;
    } else {
        status = group->group_exit_code;
    }

    // kill everyone else in the group
    struct task *task;
    list_for_each_entry(&group->threads, task, group_links) {
        deliver_signal(task, SIGKILL_, SIGINFO_NIL);
        task->group->stopped = false;
        notify(&task->group->stopped_cond);
    }

    unlock(&group->lock);
    do_exit(status);
}

// always called from init process
static void halt_system(void) {
    // brutally murder everything
    // which will leave everything in an inconsistent state. I will solve this problem later.
    for (int i = 2; i < MAX_PID; i++) {
        struct task *task = pid_get_task(i);
        if (task != NULL)
            pthread_kill(task->thread, SIGKILL);
    }

    // unmount all filesystems
    lock(&mounts_lock);
    struct mount *mount, *tmp;
    list_for_each_entry_safe(&mounts, mount, tmp, mounts) {
        mount_remove(mount);
    }
    unlock(&mounts_lock);
}

dword_t sys_exit(dword_t status) {
    STRACE("exit(%d)\n", status);
    do_exit(status << 8);
}

dword_t sys_exit_group(dword_t status) {
    STRACE("exit_group(%d)\n", status);
    do_exit_group(status << 8);
}

#define WNOHANG_ 1
#define WUNTRACED_ 2

// returns 0 if the task cannot be reaped, returns 1 if the task was reaped
static bool reap_if_zombie(struct task *task, addr_t status_addr, addr_t rusage_addr) {
    if (!task->zombie)
        return false;
    lock(&task->group->lock);

    dword_t exit_code = task->exit_code;
    if (task->group->doing_group_exit)
        exit_code = task->group->group_exit_code;
    if (status_addr != 0)
        if (user_put(status_addr, exit_code))
            return _EFAULT;

    struct rusage_ rusage = task->group->rusage;
    lock(&current->group->lock);
    rusage_add(&current->group->children_rusage, &rusage);
    unlock(&current->group->lock);
    if (rusage_addr != 0)
        if (user_put(rusage_addr, rusage))
            return _EFAULT;

    unlock(&task->group->lock);
    cond_destroy(&task->group->child_exit);
    free(task->group);
    task_destroy(task);
    return true;
}

static bool notify_if_stopped(struct task *task, addr_t status_addr) {
    // FIXME the check of task->group->stopped needs locking I think
    if (!task->group->stopped || task->group->group_exit_code == 0)
        return false;
    dword_t exit_code = task->group->group_exit_code;
    task->group->group_exit_code = 0;
    if (status_addr != 0)
        if (user_put(status_addr, exit_code))
            return _EFAULT;
    return true;
}

static bool reap_if_needed(struct task *task, int_t options, addr_t status_addr, addr_t rusage_addr) {
    assert(task_is_leader(task));
    if (options & WUNTRACED_ && notify_if_stopped(task, status_addr))
        return true;
    if (reap_if_zombie(task, status_addr, rusage_addr))
        return true;
    return false;
}

dword_t sys_wait4(dword_t id, addr_t status_addr, dword_t options, addr_t rusage_addr) {
    STRACE("wait(%d, 0x%x, 0x%x, 0x%x)", id, status_addr, options, rusage_addr);
    lock(&pids_lock);
    int err;
    pid_t_ out_id;

retry:
    if (id == (dword_t) -1) {
        // look for a zombie child
        bool no_children = true;
        struct task *parent;
        list_for_each_entry(&current->group->threads, parent, group_links) {
            struct task *task;
            list_for_each_entry(&current->children, task, siblings) {
                if (!task_is_leader(task))
                    continue;
                no_children = false;
                out_id = task->pid;
                if (reap_if_needed(task, options, status_addr, rusage_addr))
                    goto found_zombie;
            }
        }
        if (no_children) {
            err = _ECHILD;
            goto error;
        }
    } else {
        // check if this child is a zombie
        struct task *task = pid_get_task_zombie(id);
        err = _ECHILD;
        if (task == NULL || task->parent == NULL || task->parent->group != current->group)
            goto error;
        task = task->group->leader;
        out_id = id;
        if (reap_if_needed(task, options, status_addr, rusage_addr))
            goto found_zombie;
    }

    err = 0;
    if (options & WNOHANG_)
        goto error;

    // no matching zombie found, wait for one
    err = _EINTR;
    if (wait_for(&current->group->child_exit, &pids_lock, NULL))
        goto error;
    goto retry;

found_zombie:
    unlock(&pids_lock);
    return out_id;
error:
    unlock(&pids_lock);
    return err;
}

dword_t sys_waitpid(dword_t pid, addr_t status_addr, dword_t options) {
    return sys_wait4(pid, status_addr, options, 0);
}
