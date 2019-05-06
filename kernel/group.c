#include "util/list.h"
#include "kernel/calls.h"
#include "kernel/task.h"
#include "fs/tty.h"

dword_t sys_setpgid(pid_t_ id, pid_t_ pgid) {
    STRACE("setpgid(%d, %d)", id, pgid);
    int err;
    if (id == 0)
        id = current->pid;
    if (pgid == 0)
        pgid = id;
    lock(&pids_lock);
    struct pid *pid = pid_get(id);
    struct task *task = pid->task;
    err = _ESRCH;
    if (task == NULL)
        goto out;
    struct tgroup *tgroup = task->group;

    // you can only join a process group in the same session
    if (id != pgid) {
        // there has to be a process in pgrp that's in the same session as id
        err = _EPERM;
        struct pid *group_pid = pid_get(pgid);
        if (group_pid == NULL || list_empty(&group_pid->pgroup))
            goto out;
        struct tgroup *group_first_tgroup = list_first_entry(&group_pid->pgroup, struct tgroup, pgroup);
        if (tgroup->sid != group_first_tgroup->sid)
            goto out;
    }

    // you can only change the process group of yourself or a child
    err = _ESRCH;
    if (task != current && task->parent != current)
        goto out;
    // a session leader cannot create a process group
    err = _EPERM;
    if (tgroup->sid == tgroup->leader->pid)
        goto out;

    // TODO cannot set process group of a child that has done exec

    if (tgroup->pgid != pgid) {
        list_remove(&tgroup->pgroup);
        tgroup->pgid = pgid;
        list_add(&pid->pgroup, &tgroup->pgroup);
    }

    err = 0;
out:
    unlock(&pids_lock);
    return err;
}

dword_t sys_setpgrp() {
    return sys_setpgid(0, 0);
}

pid_t_ sys_getpgid(pid_t_ pid) {
    STRACE("getpgid(%d)", pid);
    if (pid != 0 && pid != current->pid)
        return _EPERM;
    lock(&pids_lock);
    pid_t_ pgid = current->group->pgid;
    unlock(&pids_lock);
    return pgid;
}
pid_t_ sys_getpgrp() {
    return sys_getpgid(0);
}

// Must lock pids_lock and task->group->lock
void task_leave_session(struct task *task) {
    struct tgroup *group = task->group;
    list_remove_safe(&group->session);
    if (group->tty) {
        lock(&ttys_lock);
        if (list_empty(&pid_get(group->sid)->session)) {
            lock(&group->tty->lock);
            group->tty->session = 0;
            unlock(&group->tty->lock);
        }
        tty_release(group->tty);
        group->tty = NULL;
        unlock(&ttys_lock);
    }
}

pid_t_ task_setsid(struct task *task) {
    lock(&pids_lock);
    struct tgroup *group = task->group;
    pid_t_ new_sid = group->leader->pid;
    if (group->pgid == new_sid || group->sid == new_sid) {
        unlock(&pids_lock);
        return _EPERM;
    }

    task_leave_session(task);
    struct pid *pid = pid_get(task->pid);
    list_add(&pid->session, &group->session);
    group->sid = new_sid;

    list_remove_safe(&group->pgroup);
    list_add(&pid->pgroup, &group->pgroup);
    group->pgid = new_sid;

    unlock(&pids_lock);
    return new_sid;
}

dword_t sys_setsid() {
    STRACE("setsid()");
    return task_setsid(current);
}

dword_t sys_getsid() {
    STRACE("getsid()");
    lock(&pids_lock);
    pid_t_ sid = current->group->sid;
    unlock(&pids_lock);
    return sid;
}

