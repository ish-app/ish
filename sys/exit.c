#include <pthread.h>
#include "sys/calls.h"

noreturn void do_exit(int status) {
    // TODO free current task structure
    printf("pid %d exit status 0x%x\n", current->pid, status);
    if (current->pid == 1) {
        exit(status >> 8);
    }
    pthread_mutex_lock(&current->parent->lock);
    current->exit_code = status;
    current->zombie = true;
    pthread_cond_broadcast(&current->parent->child_exit);
    pthread_mutex_unlock(&current->parent->lock);
    pthread_exit(NULL);
}

noreturn dword_t sys_exit(dword_t status) {
    do_exit(status << 8);
}

dword_t sys_exit_group(dword_t status) {
    sys_exit(status);
}

static int reap_if_zombie(struct process *proc, addr_t status_addr) {
    if (proc->zombie) {
        if (status_addr != 0)
            user_put(status_addr, proc->exit_code);
        process_destroy(proc);
        return 1;
    }
    return 0;
}

dword_t sys_waitpid(dword_t pid, addr_t status_addr, dword_t options) {
    if (pid == -1) {
        if (list_empty(&current->children))
            return _ESRCH;
    } else if (process_for_pid(pid) == NULL) {
        return _ESRCH;
    }

    pthread_mutex_lock(&current->lock);

    while (true) {
        if (pid == -1) {
            // look for a zombie child
            struct process *proc;
            int pid;
            list_for_each_entry(&current->children, proc, siblings) {
                pid = proc->pid;
                if (reap_if_zombie(proc, status_addr))
                    break;
            }
        } else {
            // check if this child is a zombie
            if (reap_if_zombie(process_for_pid(pid), status_addr))
                break;
        }

        // no matching zombie found, wait for one
        pthread_cond_wait(&current->child_exit, &current->lock);
    }

    pthread_mutex_unlock(&current->lock);
    return pid;
}
