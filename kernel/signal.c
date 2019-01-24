#include "debug.h"
#include <string.h>
#include <signal.h>
#include "kernel/calls.h"
#include "kernel/signal.h"
#include "kernel/vdso.h"

int xsave_extra = 0;
int fxsave_extra = 0;
static int do_sigprocmask_unlocked(dword_t how, sigset_t_ set, sigset_t_ *oldset_out);

static int signal_is_blockable(int sig) {
    return sig != SIGKILL_ && sig != SIGSTOP_;
}

#define SIGNAL_IGNORE 0
#define SIGNAL_KILL 1
#define SIGNAL_CALL_HANDLER 2
#define SIGNAL_STOP 3

static int signal_action(struct sighand *sighand, int sig) {
    if (signal_is_blockable(sig)) {
        struct sigaction_ *action = &sighand->action[sig];
        if (action->handler == SIG_IGN_)
            return SIGNAL_IGNORE;
        if (action->handler != SIG_DFL_)
            return SIGNAL_CALL_HANDLER;
    }

    switch (sig) {
        case SIGURG_: case SIGCONT_: case SIGCHLD_:
        case SIGIO_: case SIGWINCH_:
            return SIGNAL_IGNORE;

        case SIGSTOP_: case SIGTSTP_: case SIGTTIN_: case SIGTTOU_:
            return SIGNAL_STOP;

        default:
            return SIGNAL_KILL;
    }
}

void deliver_signal(struct task *task, int sig) {
    task->pending |= 1l << sig;
    if (task != current) {
        // actual madness, I hope to god it's correct
retry:
        lock(&task->waiting_cond_lock);
        if (task->waiting_cond != NULL) {
            bool mine = false;
            if (pthread_mutex_trylock(&task->waiting_lock->m) == EBUSY) {
                if (pthread_equal(task->waiting_lock->owner, pthread_self()))
                    mine = true;
                if (!mine) {
                    unlock(&task->waiting_cond_lock);
                    goto retry;
                }
            }
            notify(task->waiting_cond);
            if (!mine)
                unlock(task->waiting_lock);
        }
        unlock(&task->waiting_cond_lock);
        pthread_kill(task->thread, SIGUSR1);
    }
}

void send_signal(struct task *task, int sig) {
    // signal zero is for testing whether a process exists
    if (sig == 0)
        return;
    if (task->zombie)
        return;

    struct sighand *sighand = task->sighand;
    lock(&sighand->lock);
    if (signal_action(sighand, sig) != SIGNAL_IGNORE) {
        if (task->blocked & (1l << sig) && signal_is_blockable(sig))
            task->queued |= (1l << sig);
        else
            deliver_signal(task, sig);
    }
    unlock(&sighand->lock);

    if (sig == SIGCONT_ || sig == SIGKILL_) {
        lock(&task->group->lock);
        task->group->stopped = false;
        notify(&task->group->stopped_cond);
        unlock(&task->group->lock);
    }
}

int send_group_signal(dword_t pgid, int sig) {
    lock(&pids_lock);
    struct pid *pid = pid_get(pgid);
    if (pid == NULL) {
        unlock(&pids_lock);
        return _ESRCH;
    }
    struct tgroup *tgroup;
    list_for_each_entry(&pid->pgroup, tgroup, pgroup) {
        send_signal(tgroup->leader, sig);
    }
    unlock(&pids_lock);
    return 0;
}

static void receive_signal(struct sighand *sighand, int sig) {
    STRACE("%d receiving signal %d\n", current->pid, sig);
    current->pending &= ~(1l << sig);

    switch (signal_action(sighand, sig)) {
        case SIGNAL_IGNORE:
            return;

        case SIGNAL_STOP:
            lock(&current->group->lock);
            current->group->stopped = true;
            current->group->group_exit_code = sig << 8 | 0x7f;
            unlock(&current->group->lock);
            return;

        case SIGNAL_KILL:
            unlock(&sighand->lock); // do_exit must be called without this lock
            do_exit_group(sig);
    }

    // setup the frame
    struct sigframe_ frame = {};
    frame.sig = sig;
    frame.sc.oldmask = current->blocked & 0xffffffff;
    frame.extramask = current->blocked >> 32;

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
        die("sigreturn not found in vdso, this should never happen");
    }
    frame.pretcode = current->mm->vdso + sigreturn_addr;
    // for legacy purposes
    frame.retcode.popmov = 0xb858;
    frame.retcode.nr_sigreturn = 173; // rt_sigreturn
    frame.retcode.int80 = 0x80cd;

    // set up registers for signal handler
    cpu->eax = sig;
    cpu->eip = sighand->action[sig].handler;

    dword_t sp = cpu->esp;
    if (sighand->altstack) {
        sp = sighand->altstack + sighand->altstack_size;
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

    // block the signal while running the handler
    current->blocked |= (1l << sig);

    // install frame
    // nothing we can do if this fails
    // TODO do something other than nothing, like printk maybe
    (void) user_put(sp, frame);
}

bool receive_signals() {
    lock(&current->group->lock);
    bool was_stopped = current->group->stopped;
    unlock(&current->group->lock);

    struct sighand *sighand = current->sighand;
    lock(&sighand->lock);
    bool any_left = current->pending != 0;
    if (any_left) {
        for (int sig = 0; sig < NUM_SIGS; sig++)
            if (current->pending & (1l << sig))
                receive_signal(sighand, sig);
    }
    unlock(&sighand->lock);

    // this got moved out of the switch case in receive_signal to fix locking problems
    if (!was_stopped) {
        lock(&current->group->lock);
        bool now_stopped = current->group->stopped;
        unlock(&current->group->lock);
        if (now_stopped) {
            lock(&pids_lock);
            notify(&current->parent->group->child_exit);
            unlock(&pids_lock);
            send_signal(current->parent, current->group->leader->exit_signal);
        }
    }

    return any_left;
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
    if (--sighand->refcount == 0) {
        free(sighand);
    }
}

dword_t sys_rt_sigreturn(dword_t UNUSED(sig)) {
    struct cpu_state *cpu = &current->cpu;
    struct sigframe_ frame;
    // skip the first two fields of the frame
    // the return address was popped by the ret instruction
    // the signal number was popped into ebx and passed as an argument
    (void) user_get(cpu->esp - offsetof(struct sigframe_, sc), frame);
    // TODO check for errors in that
    cpu->eax = frame.sc.ax;
    cpu->ebx = frame.sc.bx;
    cpu->ecx = frame.sc.cx;
    cpu->edx = frame.sc.dx;
    cpu->edi = frame.sc.di;
    cpu->esi = frame.sc.si;
    cpu->ebp = frame.sc.bp;
    cpu->esp = frame.sc.sp;
    cpu->eip = frame.sc.ip;
    collapse_flags(cpu);
    cpu->eflags = frame.sc.flags;

    lock(&current->sighand->lock);
    current->sighand->on_altstack = false;
    sigset_t_ oldmask = ((sigset_t_) frame.extramask << 32) | frame.sc.oldmask;
    do_sigprocmask_unlocked(SIG_SETMASK_, oldmask, NULL);
    unlock(&current->sighand->lock);
    return cpu->eax;
}

static int do_sigaction(int sig, const struct sigaction_ *action, struct sigaction_ *oldaction) {
    if (sig >= NUM_SIGS)
        return _EINVAL;
    if (!signal_is_blockable(sig))
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

static int do_sigprocmask_unlocked(dword_t how, sigset_t_ set, sigset_t_ *oldset_out) {
    sigset_t_ oldset = current->blocked;

    if (how == SIG_BLOCK_)
        current->blocked |= set;
    else if (how == SIG_UNBLOCK_)
        current->blocked &= ~set;
    else if (how == SIG_SETMASK_)
        current->blocked = set;
    else
        return _EINVAL;

    // transfer unblocked signals from queued to pending
    sigset_t_ unblocked = oldset & ~current->blocked;
    current->pending |= current->queued & unblocked;
    current->queued &= ~unblocked;
    // transfer blocked signals from pending to queued
    sigset_t_ blocked = current->blocked & ~oldset;
    current->queued |= current->pending & blocked;
    current->pending &= ~blocked;

    if (oldset_out != NULL)
        *oldset_out = oldset;
    return 0;
}

int do_sigprocmask(dword_t how, sigset_t_ set, sigset_t_ *oldset_out) {
    struct sighand *sighand = current->sighand;
    lock(&sighand->lock);
    int res = do_sigprocmask_unlocked(how, set, oldset_out);
    unlock(&sighand->lock);
    return res;
}

dword_t sys_rt_sigprocmask(dword_t how, addr_t set_addr, addr_t oldset_addr, dword_t size) {
    if (size != sizeof(sigset_t_))
        return _EINVAL;

    sigset_t_ set;
    if (set_addr != 0)
        if (user_get(set_addr, set))
            return _EFAULT;
    STRACE("rt_sigprocmask(%s, %#llx, %#x, %d)",
            how == SIG_BLOCK_ ? "SIG_BLOCK" :
            how == SIG_UNBLOCK_ ? "SIG_UNBLOCK" :
            how == SIG_SETMASK_ ? "SIG_SETMASK" : "??",
            set_addr != 0 ? (long long) set : -1, oldset_addr, size);

    if (oldset_addr != 0)
        if (user_put(oldset_addr, current->blocked))
            return _EFAULT;
    if (set_addr != 0) {
        int err = do_sigprocmask(how, set, NULL);
        if (err < 0)
            return err;
    }
    return 0;
}

int_t sys_rt_sigpending(addr_t set_addr) {
    STRACE("rt_sigpending(%#x)");
    if (user_put(set_addr, current->pending))
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

int_t sys_rt_sigsuspend(addr_t mask_addr, uint_t size) {
    if (size != sizeof(sigset_t_))
        return _EINVAL;
    sigset_t_ mask, oldmask;
    if (user_get(mask_addr, mask))
        return _EFAULT;
    STRACE("sigsuspend(0x%llx)\n", (long long) mask);

    lock(&current->sighand->lock);
    do_sigprocmask_unlocked(SIG_SETMASK_, mask, &oldmask);
    while (!current->pending)
        wait_for(&current->pause, &current->sighand->lock, NULL);
    do_sigprocmask_unlocked(SIG_SETMASK_, oldmask, NULL);
    unlock(&current->sighand->lock);
    return _EINTR;
}

dword_t sys_kill(pid_t_ pid, dword_t sig) {
    STRACE("kill(%d, %d)", pid, sig);
    if (sig >= NUM_SIGS)
        return _EINVAL;
    // TODO check permissions
    if (pid == 0)
        pid = -current->group->pgid;
    if (pid < 0)
        return send_group_signal(-pid, sig);

    lock(&pids_lock);
    struct task *task = pid_get_task(pid);
    if (task == NULL) {
        unlock(&pids_lock);
        return _ESRCH;
    }
    send_signal(task, sig);
    unlock(&pids_lock);
    return 0;
}

dword_t sys_tkill(pid_t_ tid, dword_t sig) {
    return sys_kill(tid, sig);
}
