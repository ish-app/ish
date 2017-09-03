#include "util/list.h"
#include "sys/calls.h"
#include "sys/process.h"

dword_t sys_setpgid(dword_t id, dword_t pgid) {
    int err;
    if (id == 0)
        id = current->pid;
    if (pgid == 0)
        pgid = id;
    struct pid *pid = pid_get(id);
    lock(pid);

    // when creating a process group, you need to specify your own pid
    // TODO or a child in the same session
    err = _EPERM;
    if (list_empty(&pid->group) && id != pgid)
        goto unlock_pid;
    // TODO you can only join a process group in the same session

    struct process *proc = pid->proc;
    err = _ESRCH;
    if (proc == NULL)
        goto unlock_pid;

    lock(proc);

    // you can only change the process group of yourself or a child
    err = _ESRCH;
    if (proc != current && proc->parent != current)
        goto unlock_proc;
    // a session leader cannot create a process group
    err = _EPERM;
    if (proc->sid == proc->pid)
        goto unlock_proc;

    // TODO cannot set process group of a child that has done exec

    if (proc->pgid != pgid) {
        struct pid *old_group = pid_get(proc->pgid);
        lock(old_group);
        list_remove(&proc->group);
        unlock(old_group);
        proc->pgid = pgid;
        list_add(&pid->group, &proc->group);
    }

    err = 0;
unlock_proc:
    unlock(proc);
unlock_pid:
    unlock(pid);
    return err;
}

dword_t sys_setpgrp() {
    return sys_setpgid(0, 0);
}

dword_t sys_setsid() {
    lock(current);
    if (current->pgid == current->pid || current->sid == current->pid) {
        unlock(current);
        return _EPERM;
    }

    struct pid *pid = pid_get(current->pid);
    lock(pid);
    list_add(&pid->session, &current->session);
    current->sid = current->pid;
    list_add(&pid->group, &current->group);
    current->pgid = current->pid;
    unlock(pid);

    unlock(current);
    return 0;
}
