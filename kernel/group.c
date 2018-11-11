#include "util/list.h"
#include "kernel/calls.h"
#include "kernel/task.h"

dword_t sys_setpgid(dword_t id, dword_t pgid) {
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

    // you can only join a process group in the same session
    if (id != pgid) {
        // there has to be a process in pgrp that's in the same session as id
        err = _EPERM;
        struct pid *group_pid = pid_get(pgid);
        if (list_empty(&group_pid->pgroup))
            goto out;
        struct task *group_task = list_first_entry(&group_pid->pgroup, struct task, pgroup);
        if (group_task->sid != task->sid)
            goto out;
    }

    // you can only change the process group of yourself or a child
    err = _ESRCH;
    if (task != current && task->parent != current)
        goto out;
    // a session leader cannot create a process group
    err = _EPERM;
    if (task->sid == task->pid)
        goto out;

    // TODO cannot set process group of a child that has done exec

    if (task->pgid != pgid) {
        list_remove(&task->pgroup);
        task->pgid = pgid;
        list_add(&pid->pgroup, &task->pgroup);
    }

    err = 0;
out:
    unlock(&pids_lock);
    return err;
}

dword_t sys_setpgrp() {
    return sys_setpgid(0, 0);
}

dword_t sys_setsid() {
    lock(&pids_lock);
    if (current->pgid == current->pid || current->sid == current->pid) {
        unlock(&pids_lock);
        return _EPERM;
    }

    struct pid *pid = pid_get(current->pid);
    list_add(&pid->session, &current->session);
    current->sid = current->pid;
    list_add(&pid->pgroup, &current->pgroup);
    current->pgid = current->pid;

    unlock(&pids_lock);
    return 0;
}

dword_t sys_getsid() {
    pid_t_ sid;
    lock(&pids_lock);
    sid = current->sid;
    unlock(&pids_lock);
    return sid;
}
