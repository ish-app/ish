#include "debug.h"
#include <signal.h>
#include "kernel/calls.h"
#include "kernel/signal.h"
#include "kernel/vdso.h"

int xsave_extra = 0;
int fxsave_extra = 0;

void deliver_signal(struct task *task, int sig) {
    task->pending |= 1l << sig;
    if (task != current) {
        // actual madness, I hope to god it's correct
retry:
        lock(&task->waiting_cond_lock);
        if (task->waiting_cond != NULL) {
            if (pthread_mutex_trylock(task->waiting_lock)) {
                unlock(&task->waiting_cond_lock);
                goto retry;
            }
            notify(task->waiting_cond);
            unlock(task->waiting_lock);
        }
        unlock(&task->waiting_cond_lock);
        pthread_kill(task->thread, SIGUSR1);
    }
}

static int signal_is_blockable(int sig) {
    return sig != SIGKILL_ && sig != SIGSTOP_;
}

void send_signal(struct task *task, int sig) {
    struct sighand *sighand = task->sighand;
    lock(&sighand->lock);
    if (sighand->action[sig].handler != SIG_IGN_) {
        if (task->blocked & (1l << sig) && signal_is_blockable(sig))
            task->queued |= (1l << sig);
        else
            deliver_signal(task, sig);
    }
    unlock(&sighand->lock);
}

void send_group_signal(dword_t pgid, int sig) {
    lock(&pids_lock);
    struct pid *pid = pid_get(pgid);
    if (pid == NULL) {
        unlock(&pids_lock);
        return;
    }
    struct task *task;
    list_for_each_entry(&pid->pgroup, task, pgroup) {
        send_signal(task, sig);
    }
    unlock(&pids_lock);
}

static void receive_signal(struct sighand *sighand, int sig) {
    STRACE("%d receiving signal %d\n", current->pid, sig);
    if (sighand->action[sig].handler == SIG_DFL_) {
        switch (sig) {
            // non-fatal signals
            case SIGURG_: case SIGCONT_: case SIGCHLD_:
            case SIGIO_: case SIGWINCH_:
                break;

            // most of the rest are fatal
            // some stop the process, we'll leave that as unimplemented
            default:
                unlock(&sighand->lock); // do_exit must be called without this lock
                do_exit_group(sig);
        }
        return;
    }

    // setup the frame
    struct sigframe_ frame = {};
    frame.sig = sig;

    struct cpu_state *cpu = &current->cpu;
    frame.sc.ax = cpu->eax;
    frame.sc.bx = cpu->ebx;
    frame.sc.cx = cpu->ecx;
    frame.sc.dx = cpu->edx;
    frame.sc.di = cpu->edi;
    frame.sc.si = cpu->esi;
    frame.sc.bp = cpu->ebp;
    frame.sc.sp = frame.sc.sp_at_signal = cpu->esp;
    frame.sc.ip = cpu->eip;
    collapse_flags(cpu);
    frame.sc.flags = cpu->eflags;
    frame.sc.trapno = cpu->trapno;
    // TODO more shit

    addr_t sigreturn_addr = vdso_symbol("__kernel_rt_sigreturn");
    if (sigreturn_addr == 0) {
        fprintf(stderr, "sigreturn not found in vdso, this should never happen\n");
        abort();
    }
    frame.pretcode = current->mem->vdso + sigreturn_addr;
    // for legacy purposes
    frame.retcode.popmov = 0xb858;
    frame.retcode.nr_sigreturn = 173; // rt_sigreturn
    frame.retcode.int80 = 0x80cd;

    // set up registers for signal handler
    cpu->eax = sig;
    cpu->eip = sighand->action[sig].handler;

    dword_t sp = cpu->esp;
    if (sighand->altstack) {
        sp = sighand->altstack;
        sighand->on_altstack = true;
    }
    if (xsave_extra) {
        // do as the kernel does
        // this is superhypermega condensed version of fpu__alloc_mathframe in
        // arch/x86/kernel/fpu/signal.c
        sp -= xsave_extra;
        sp &=~ 0x3f;
        sp -= fxsave_extra;
    }
    sp -= sizeof(struct sigframe_);
    // align sp + 4 on a 16-byte boundary because that's what the abi says
	sp = ((sp + 4) & ~0xf) - 4;
    cpu->esp = sp;

    // install frame
    // nothing we can do if this fails
    // TODO do something other than nothing, like printk maybe
    (void) user_put(sp, frame);

    current->pending &= ~(1l << sig);
}

void receive_signals() {
    struct sighand *sighand = current->sighand;
    lock(&sighand->lock);
    if (current->pending) {
        for (int sig = 0; sig < NUM_SIGS; sig++)
            if (current->pending & (1l << sig))
                receive_signal(sighand, sig);
    }
    unlock(&sighand->lock);
}

struct sighand *sighand_new() {
    struct sighand *sighand = malloc(sizeof(struct sighand));
    if (sighand == NULL)
        return NULL;
    memset(sighand, 0, sizeof(struct sighand));
    sighand->refcount = 1;
    lock_init(&sighand->lock);
    return sighand;
}

struct sighand *sighand_copy(struct sighand *sighand) {
    struct sighand *new_sighand = sighand_new();
    if (new_sighand == NULL)
        return NULL;
    memcpy(new_sighand->action, sighand->action, sizeof(new_sighand->action));
    return new_sighand;
}

void sighand_release(struct sighand *sighand) {
    if (--sighand->refcount== 0) {
        free(sighand);
    }
}

dword_t sys_rt_sigreturn(dword_t sig) {
    struct cpu_state *cpu = &current->cpu;
    struct sigcontext_ sc;
    // skip the first two fields of the frame
    // the return address was popped by the ret instruction
    // the signal number was popped into ebx and passed as an argument
    (void) user_get(cpu->esp, sc);
    // TODO check for errors in that
    cpu->eax = sc.ax;
    cpu->ebx = sc.bx;
    cpu->ecx = sc.cx;
    cpu->edx = sc.dx;
    cpu->edi = sc.di;
    cpu->esi = sc.si;
    cpu->ebp = sc.bp;
    cpu->esp = sc.sp;
    cpu->eip = sc.ip;
    collapse_flags(cpu);
    cpu->eflags = sc.flags;
    lock(&current->sighand->lock);
    current->sighand->on_altstack = false;
    unlock(&current->sighand->lock);
    return cpu->eax;
}

static int do_sigaction(int sig, const struct sigaction_ *action, struct sigaction_ *oldaction) {
    if (sig >= NUM_SIGS)
        return _EINVAL;
    if (sig == SIGKILL_ || sig == SIGSTOP_)
        return _EINVAL;

    struct sighand *sighand = current->sighand;
    lock(&sighand->lock);
    if (oldaction)
        *oldaction = sighand->action[sig];
    if (action)
        sighand->action[sig] = *action;
    unlock(&sighand->lock);
    return 0;
}

dword_t sys_rt_sigaction(dword_t signum, addr_t action_addr, addr_t oldaction_addr, dword_t sigset_size) {
    if (sigset_size != sizeof(sigset_t_))
        return _EINVAL;
    struct sigaction_ action, oldaction;
    if (action_addr != 0)
        if (user_get(action_addr, action))
            return _EFAULT;
    STRACE("rt_sigaction(%d, 0x%x, 0x%x, %d)", signum, action_addr, oldaction_addr, sigset_size);

    int err = do_sigaction(signum,
            action_addr ? &action : NULL,
            oldaction_addr ? &oldaction : NULL);
    if (err < 0)
        return err;

    if (oldaction_addr != 0)
        if (user_put(oldaction_addr, oldaction))
            return _EFAULT;
    return err;
}

dword_t sys_sigaction(dword_t signum, addr_t action_addr, addr_t oldaction_addr) {
    return sys_rt_sigaction(signum, action_addr, oldaction_addr, 1);
}

int do_sigprocmask(dword_t how, sigset_t_ set, sigset_t_ *oldset_out) {
    struct sighand *sighand = current->sighand;
    lock(&sighand->lock);
    sigset_t_ oldset = current->blocked;

    if (how == SIG_BLOCK_)
        current->blocked |= set;
    else if (how == SIG_UNBLOCK_)
        current->blocked &= ~set;
    else if (how == SIG_SETMASK_)
        current->blocked = set;
    else {
        unlock(&sighand->lock);
        return _EINVAL;
    }

    // transfer unblocked signals from queued to pending
    sigset_t_ unblocked = oldset & ~current->blocked;
    current->pending |= current->queued & unblocked;
    current->queued &= ~unblocked;
    // transfer blocked signals from pending to queued
    sigset_t_ blocked = current->blocked & ~oldset;
    current->queued |= current->pending & blocked;
    current->pending &= ~blocked;

    unlock(&sighand->lock);
    if (oldset_out != NULL)
        *oldset_out = oldset;
    return 0;
}

dword_t sys_rt_sigprocmask(dword_t how, addr_t set_addr, addr_t oldset_addr, dword_t size) {
    if (size != sizeof(sigset_t_))
        return _EINVAL;

    sigset_t_ set, oldset;
    if (user_get(set_addr, set))
        return _EFAULT;
    STRACE("rt_sigprocmask(%s, 0x%llx, 0x%x, %d)",
            how == SIG_BLOCK_ ? "SIG_BLOCK" :
            how == SIG_UNBLOCK_ ? "SIG_UNBLOCK" :
            how == SIG_SETMASK_ ? "SIG_SETMASK" : "??",
            (long long) set, oldset_addr, size);

    int err = do_sigprocmask(how, set, &oldset);
    if (err < 0)
        return err;
    if (oldset_addr != 0)
        if (user_put(oldset_addr, oldset))
            return _EFAULT;
    return 0;
}

dword_t sys_sigaltstack(addr_t ss_addr, addr_t old_ss_addr) {
    STRACE("sigaltstack(0x%x, 0x%x)", ss_addr, old_ss_addr);
    struct sighand *sighand = current->sighand;
    lock(&sighand->lock);
    if (old_ss_addr != 0) {
        struct stack_t_ old_ss;
        old_ss.stack = sighand->altstack;
        old_ss.size = sighand->altstack_size;
        old_ss.flags = 0;
        if (sighand->altstack == 0)
            old_ss.flags |= SS_DISABLE_;
        if (sighand->on_altstack)
            old_ss.flags |= SS_ONSTACK_;
        if (user_put(old_ss_addr, old_ss)) {
            unlock(&sighand->lock);
            return _EFAULT;
        }
    }
    if (ss_addr != 0) {
        if (sighand->on_altstack) {
            unlock(&sighand->lock);
            return _EPERM;
        }
        struct stack_t_ ss;
        if (user_get(ss_addr, ss)) {
            unlock(&sighand->lock);
            return _EFAULT;
        }
        if (ss.flags & SS_DISABLE_) {
            sighand->altstack = 0;
        } else {
            sighand->altstack = ss.stack;
            sighand->altstack_size = ss.size;
        }
    }
    unlock(&sighand->lock);
    return 0;
}

dword_t sys_kill(dword_t pid, dword_t sig) {
    STRACE("kill(%d, %d)", pid, sig);
    // TODO check permissions
    // TODO process groups
    lock(&pids_lock);
    struct task *task = pid_get_task(pid);
    if (task == NULL) {
        unlock(&pids_lock);
        return _ESRCH;
    }
    // signal zero is for testing whether a process exists
    if (sig != 0)
        send_signal(task, sig);
    unlock(&pids_lock);
    return 0;
}

dword_t sys_tkill(dword_t tid, dword_t sig) {
    return sys_kill(tid, sig);
}
