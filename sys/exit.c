#include <pthread.h>
#include "sys/calls.h"

noreturn void do_exit(int status) {
    if (current->pid == 1) {
        exit(status >> 8);
    }
    pthread_mutex_lock(&current->parent->lock);
    current->exit_code = status;
    current->zombie = true;
    pthread_cond_broadcast(&current->parent->child_exit);
    pthread_mutex_unlock(&current->parent->lock);

    pthread_mutex_lock(&current->lock);
    pthread_cond_broadcast(&current->vfork_done);
    pthread_mutex_unlock(&current->lock);

    pthread_exit(NULL);
}

noreturn dword_t sys_exit(dword_t status) {
    do_exit(status << 8);
}

dword_t sys_exit_group(dword_t status) {
    sys_exit(status);
}

struct rusage_ {
    struct timeval_ utime;
    struct timeval_ stime;
    dword_t maxrss;
    dword_t ixrss;
    dword_t idrss;
    dword_t isrss;
    dword_t minflt;
    dword_t majflt;
    dword_t nswap;
    dword_t inblock;
    dword_t oublock;
    dword_t msgsnd;
    dword_t msgrcv;
    dword_t nsignals;
    dword_t nvcsw;
    dword_t nivcsw;
};

static int reap_if_zombie(struct process *proc, addr_t status_addr, addr_t rusage_addr) {
    if (proc->zombie) {
        if (status_addr != 0)
            if (user_put(status_addr, proc->exit_code))
                return _EFAULT;
        if (rusage_addr != 0) {
            struct rusage_ rusage = {};
            if (user_put(rusage_addr, rusage))
                return _EFAULT;
        }
        process_destroy(proc);
        return 1;
    }
    return 0;
}

dword_t sys_wait4(dword_t pid, addr_t status_addr, dword_t options, addr_t rusage_addr) {
    if (pid == (dword_t) -1) {
        if (list_empty(&current->children))
            return _ESRCH;
    } else if (process_for_pid(pid) == NULL) {
        return _ESRCH;
    }

    pthread_mutex_lock(&current->lock);

retry:
    if (pid == (dword_t) -1) {
        // look for a zombie child
        struct process *proc;
        list_for_each_entry(&current->children, proc, siblings) {
            pid = proc->pid;
            if (reap_if_zombie(proc, status_addr, rusage_addr))
                goto found_zombie;
        }
    } else {
        // check if this child is a zombie
        if (reap_if_zombie(process_for_pid(pid), status_addr, rusage_addr))
            goto found_zombie;
    }

    // no matching zombie found, wait for one
    pthread_cond_wait(&current->child_exit, &current->lock);
    goto retry;

found_zombie:
    pthread_mutex_unlock(&current->lock);
    return pid;
}

dword_t sys_waitpid(dword_t pid, addr_t status_addr, dword_t options) {
    return sys_wait4(pid, status_addr, options, 0);
}
