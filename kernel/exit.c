#include <pthread.h>
#include <signal.h>
#include "kernel/calls.h"

void (*exit_hook)(int code) = NULL;

noreturn void do_exit(int status) {
    // this is the part where we release all our resources
    mem_release(current->cpu.mem);
    for (fd_t f = 0; f < MAX_FD; f++) {
        if (current->files[f]) {
            fd_close(current->files[f]);
            current->files[f] = NULL;
        }
    }

    // notify everyone we died
    bool init_died = current->pid == 1;
    lock(pids_lock);
    struct process *parent = current->parent;
    unlock(pids_lock);
    lock(parent->exit_lock);
    current->exit_code = status;
    current->zombie = true;
    notify(parent->child_exit);
    unlock(parent->exit_lock);
    notify(current->vfork_done);

    if (init_died) {
        // brutally murder everything
        // which will leave everything in an inconsistent state. I will solve this problem later.
        lock(pids_lock);
        for (int i = 2; i < MAX_PID; i++) {
            struct process *proc = pid_get_proc(i);
            if (proc != NULL)
                pthread_kill(proc->thread, SIGKILL);
        }
        unlock(pids_lock);

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
    sleep(1000000000);
    pthread_exit(NULL);
}

dword_t sys_exit(dword_t status) {
    do_exit(status << 8);
}

dword_t sys_exit_group(dword_t status) {
    do_exit(status << 8);
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

#define WNOHANG_ 1

dword_t sys_wait4(dword_t id, addr_t status_addr, dword_t options, addr_t rusage_addr) {
    STRACE("wait(%d, 0x%x, 0x%x, 0x%x)", id, status_addr, options, rusage_addr);
    lock(current->exit_lock);

retry:
    if (id == (dword_t) -1) {
        // look for a zombie child
        struct process *proc;
        list_for_each_entry(&current->children, proc, siblings) {
            id = proc->pid;
            if (reap_if_zombie(proc, status_addr, rusage_addr))
                goto found_zombie;
        }
    } else {
        // check if this child is a zombie
        struct process *proc = pid_get_proc(id);
        if (proc == NULL || proc->parent != current) {
            unlock(current->exit_lock);
            return _ECHILD;
        }
        if (reap_if_zombie(proc, status_addr, rusage_addr))
            goto found_zombie;
    }

    if (options & WNOHANG_) {
        unlock(current->exit_lock);
        return _ECHILD;
    }

    // no matching zombie found, wait for one
    wait_for(current->child_exit, current->exit_lock);
    goto retry;

found_zombie:
    unlock(current->exit_lock);
    return id;
}

dword_t sys_waitpid(dword_t pid, addr_t status_addr, dword_t options) {
    return sys_wait4(pid, status_addr, options, 0);
}
