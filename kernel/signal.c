#include "debug.h"
#include <string.h>
#include <signal.h>
#include "kernel/calls.h"
#include "kernel/signal.h"
#include "kernel/vdso.h"
#include "emu/interrupt.h"

int xsave_extra = 0;
int fxsave_extra = 0;
static void sigmask_set(sigset_t_ set);
static void altstack_to_user(struct sighand *sighand, struct stack_t_ *user_stack);
static bool is_on_altstack(dword_t sp, struct sighand *sighand);

static int signal_is_blockable(int sig) {
    return sig != SIGKILL_ && sig != SIGSTOP_;
}

#define UNBLOCKABLE_MASK (sig_mask(SIGKILL_) | sig_mask(SIGSTOP_))

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

static void deliver_signal_unlocked(struct task *task, int sig, struct siginfo_ info) {
    if (sigset_has(task->pending, sig))
        return;

    sigset_add(&task->pending, sig);
    struct sigqueue *sigqueue = malloc(sizeof(struct sigqueue));
    sigqueue->info = info;
    sigqueue->info.sig = sig;
    list_add_tail(&task->queue, &sigqueue->queue);

    if (sigset_has(task->blocked, sig) && signal_is_blockable(sig))
        return;

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

void deliver_signal(struct task *task, int sig, struct siginfo_ info) {
    lock(&task->sighand->lock);
    deliver_signal_unlocked(task, sig, info);
    unlock(&task->sighand->lock);
}

void send_signal(struct task *task, int sig, struct siginfo_ info) {
    // signal zero is for testing whether a process exists
    if (sig == 0)
        return;
    if (task->zombie)
        return;

    struct sighand *sighand = task->sighand;
    lock(&sighand->lock);
    if (signal_action(sighand, sig) != SIGNAL_IGNORE) {
        deliver_signal_unlocked(task, sig, info);
    }
    unlock(&sighand->lock);

    if (sig == SIGCONT_ || sig == SIGKILL_) {
        lock(&task->group->lock);
        task->group->stopped = false;
        notify(&task->group->stopped_cond);
        unlock(&task->group->lock);
    }
}

bool try_self_signal(int sig) {
    assert(sig == SIGTTIN_ || sig == SIGTTOU_);

    struct sighand *sighand = current->sighand;
    lock(&sighand->lock);
    bool can_send = signal_action(sighand, sig) != SIGNAL_IGNORE &&
        !sigset_has(current->blocked, sig);
    if (can_send)
        deliver_signal_unlocked(current, sig, SIGINFO_NIL);
    unlock(&sighand->lock);
    return can_send;
}

int send_group_signal(dword_t pgid, int sig, struct siginfo_ info) {
    lock(&pids_lock);
    struct pid *pid = pid_get(pgid);
    if (pid == NULL) {
        unlock(&pids_lock);
        return _ESRCH;
    }
    struct tgroup *tgroup;
    list_for_each_entry(&pid->pgroup, tgroup, pgroup) {
        send_signal(tgroup->leader, sig, info);
    }
    unlock(&pids_lock);
    return 0;
}

static addr_t sigreturn_trampoline(const char *name) {
    addr_t sigreturn_addr = vdso_symbol(name);
    if (sigreturn_addr == 0) {
        die("sigreturn not found in vdso, this should never happen");
    }
    return current->mm->vdso + sigreturn_addr;
}

static void setup_sigcontext(struct sigcontext_ *sc, struct cpu_state *cpu) {
    sc->ax = cpu->eax;
    sc->bx = cpu->ebx;
    sc->cx = cpu->ecx;
    sc->dx = cpu->edx;
    sc->di = cpu->edi;
    sc->si = cpu->esi;
    sc->bp = cpu->ebp;
    sc->sp = sc->sp_at_signal = cpu->esp;
    sc->ip = cpu->eip;
    collapse_flags(cpu);
    sc->flags = cpu->eflags;
    sc->trapno = cpu->trapno;
    if (cpu->trapno == INT_GPF)
        sc->cr2 = cpu->segfault_addr;
    // TODO more shit
    sc->oldmask = current->blocked & 0xffffffff;
}

static void setup_sigframe(struct siginfo_ *info, struct sigframe_ *frame) {
    frame->restorer = sigreturn_trampoline("__kernel_sigreturn");
    frame->sig = info->sig;
    setup_sigcontext(&frame->sc, &current->cpu);
    frame->extramask = current->blocked >> 32;

    static const struct {
        uint16_t popmov;
        uint32_t nr_sigreturn;
        uint16_t int80;
    } __attribute__((packed)) retcode = {
        .popmov = 0xb858,
        .nr_sigreturn = 113,
        .int80 = 0x80cd,
    };
    memcpy(frame->retcode, &retcode, sizeof(retcode));
}

static void setup_rt_sigframe(struct siginfo_ *info, struct rt_sigframe_ *frame) {
    frame->restorer = sigreturn_trampoline("__kernel_rt_sigreturn");
    frame->sig = info->sig;
    frame->info = *info;
    frame->uc.flags = 0;
    frame->uc.link = 0;
    altstack_to_user(current->sighand, &frame->uc.stack);
    setup_sigcontext(&frame->uc.mcontext, &current->cpu);
    frame->uc.sigmask = current->blocked;

    static const struct {
        uint8_t mov;
        uint32_t nr_rt_sigreturn;
        uint16_t int80;
        uint8_t pad;
    } __attribute__((packed)) rt_retcode = {
        .mov = 0xb8,
        .nr_rt_sigreturn = 173,
        .int80 = 0x80cd,
    };
    memcpy(frame->retcode, &rt_retcode, sizeof(rt_retcode));
}

static void receive_signal(struct sighand *sighand, struct siginfo_ *info) {
    int sig = info->sig;
    STRACE("%d receiving signal %d\n", current->pid, sig);
    sigset_del(&current->pending, sig);

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

    struct sigaction_ *action = &sighand->action[info->sig];
    bool need_siginfo = action->flags & SA_SIGINFO_;

    // setup the frame
    union {
        struct sigframe_ sigframe;
        struct rt_sigframe_ rt_sigframe;
    } frame = {};
    size_t frame_size;
    if (need_siginfo) {
        setup_rt_sigframe(info, &frame.rt_sigframe);
        frame_size = sizeof(frame.rt_sigframe);
    } else {
        setup_sigframe(info, &frame.sigframe);
        frame_size = sizeof(frame.sigframe);
    }

    // set up registers for signal handler
    current->cpu.eax = info->sig;
    current->cpu.eip = sighand->action[info->sig].handler;

    dword_t sp = current->cpu.esp;
    if (sighand->altstack) {
        sp = sighand->altstack + sighand->altstack_size;
    }
    if (xsave_extra) {
        // do as the kernel does
        // this is superhypermega condensed version of fpu__alloc_mathframe in
        // arch/x86/kernel/fpu/signal.c
        sp -= xsave_extra;
        sp &=~ 0x3f;
        sp -= fxsave_extra;
    }
    sp -= frame_size;
    // align sp + 4 on a 16-byte boundary because that's what the abi says
    sp = ((sp + 4) & ~0xf) - 4;
    current->cpu.esp = sp;

    // Update the mask. By default the signal will be blocked while in the
    // handler, but sigaction is allowed to customize this.
    if (!(action->flags & SA_NODEFER_))
        sigset_add(&current->blocked, info->sig);
    current->blocked |= action->mask;

    // these have to be filled in after the location of the frame is known
    if (need_siginfo) {
        frame.rt_sigframe.pinfo = sp + offsetof(struct rt_sigframe_, info);
        frame.rt_sigframe.puc = sp + offsetof(struct rt_sigframe_, uc);
        current->cpu.edx = frame.rt_sigframe.pinfo;
        current->cpu.ecx = frame.rt_sigframe.puc;
    }

    // install frame
    (void) user_write(sp, &frame, frame_size);
    // nothing we can do if that fails
    // TODO do something other than nothing, like printk maybe
}

void receive_signals() {
    lock(&current->group->lock);
    bool was_stopped = current->group->stopped;
    unlock(&current->group->lock);

    struct sighand *sighand = current->sighand;
    lock(&sighand->lock);

    // A saved mask means that the last system call was a call like sigsuspend
    // that changes the mask during the call. Only ignore a signal right now if
    // it was both blocked during the call and should still be blocked after
    // the call.
    sigset_t_ blocked = current->blocked;
    if (current->has_saved_mask) {
        blocked &= current->saved_mask;
        current->has_saved_mask = false;
        current->blocked = current->saved_mask;
    }

    struct sigqueue *sigqueue, *tmp;
    list_for_each_entry_safe(&current->queue, sigqueue, tmp, queue) {
        if (sigset_has(blocked, sigqueue->info.sig))
            continue;
        list_remove(&sigqueue->queue);
        receive_signal(sighand, &sigqueue->info);
        free(sigqueue);
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
            // TODO add siginfo
            send_signal(current->parent, current->group->leader->exit_signal, SIGINFO_NIL);
            unlock(&pids_lock);
        }
    }
}

static void restore_sigcontext(struct sigcontext_ *context, struct cpu_state *cpu) {
    cpu->eax = context->ax;
    cpu->ebx = context->bx;
    cpu->ecx = context->cx;
    cpu->edx = context->dx;
    cpu->edi = context->di;
    cpu->esi = context->si;
    cpu->ebp = context->bp;
    cpu->esp = context->sp;
    cpu->eip = context->ip;
    collapse_flags(cpu);

    // Use AC, RF, OF, DF, TF, SF, ZF, AF, PF, CF
#define USE_FLAGS 0b1010000110111010101
    cpu->eflags = (context->flags & USE_FLAGS) | (cpu->eflags & ~USE_FLAGS);
}

dword_t sys_rt_sigreturn() {
    struct cpu_state *cpu = &current->cpu;
    struct rt_sigframe_ frame;
    // esp points past the first field of the frame
    (void) user_get(cpu->esp - offsetof(struct rt_sigframe_, sig), frame);
    restore_sigcontext(&frame.uc.mcontext, cpu);

    lock(&current->sighand->lock);
    // FIXME this duplicates logic from sys_sigaltstack
    if (!is_on_altstack(cpu->esp, current->sighand) &&
            frame.uc.stack.size >= MINSIGSTKSZ_) {
        current->sighand->altstack = frame.uc.stack.stack;
        current->sighand->altstack_size = frame.uc.stack.size;
    }
    sigmask_set(frame.uc.sigmask);
    unlock(&current->sighand->lock);
    return cpu->eax;
}

dword_t sys_sigreturn() {
    struct cpu_state *cpu = &current->cpu;
    struct sigframe_ frame;
    // esp points past the first two fields of the frame
    (void) user_get(cpu->esp - offsetof(struct sigframe_, sc), frame);
    // TODO check for errors in that
    restore_sigcontext(&frame.sc, cpu);

    lock(&current->sighand->lock);
    sigset_t_ oldmask = ((sigset_t_) frame.extramask << 32) | frame.sc.oldmask;
    sigmask_set(oldmask);
    unlock(&current->sighand->lock);
    return cpu->eax;
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

static void sigmask_set(sigset_t_ set) {
    current->blocked = (set & ~UNBLOCKABLE_MASK);
}

static void sigmask_set_temp_unlocked(sigset_t_ mask) {
    current->saved_mask = current->blocked;
    current->has_saved_mask = true;
    sigmask_set(mask);
}

void sigmask_set_temp(sigset_t_ mask) {
    lock(&current->sighand->lock);
    sigmask_set_temp_unlocked(mask);
    unlock(&current->sighand->lock);
}

static int do_sigprocmask(dword_t how, sigset_t_ set) {
    if (how == SIG_BLOCK_)
        sigmask_set(current->blocked | set);
    else if (how == SIG_UNBLOCK_)
        sigmask_set(current->blocked & ~set);
    else if (how == SIG_SETMASK_)
        sigmask_set(set);
    else
        return _EINVAL;
    return 0;
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
        struct sighand *sighand = current->sighand;
        lock(&sighand->lock);
        int err = do_sigprocmask(how, set);
        unlock(&sighand->lock);
        if (err < 0)
            return err;
    }
    return 0;
}

int_t sys_rt_sigpending(addr_t set_addr) {
    STRACE("rt_sigpending(%#x)");
    // as defined by the standard
    sigset_t_ pending = current->pending & current->blocked;
    if (user_put(set_addr, pending))
        return _EFAULT;
    return 0;
}

static bool is_on_altstack(dword_t sp, struct sighand *sighand) {
    return sp > sighand->altstack && sp <= sighand->altstack + sighand->altstack_size;
}

static void altstack_to_user(struct sighand *sighand, struct stack_t_ *user_stack) {
    user_stack->stack = sighand->altstack;
    user_stack->size = sighand->altstack_size;
    user_stack->flags = 0;
    if (sighand->altstack == 0)
        user_stack->flags |= SS_DISABLE_;
    if (is_on_altstack(current->cpu.esp, sighand))
        user_stack->flags |= SS_ONSTACK_;
}

dword_t sys_sigaltstack(addr_t ss_addr, addr_t old_ss_addr) {
    STRACE("sigaltstack(0x%x, 0x%x)", ss_addr, old_ss_addr);
    struct sighand *sighand = current->sighand;
    lock(&sighand->lock);
    if (old_ss_addr != 0) {
        struct stack_t_ old_ss;
        altstack_to_user(sighand, &old_ss);
        if (user_put(old_ss_addr, old_ss)) {
            unlock(&sighand->lock);
            return _EFAULT;
        }
    }
    if (ss_addr != 0) {
        if (is_on_altstack(current->cpu.esp, sighand)) {
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
            if (ss.size < MINSIGSTKSZ_)
                return _ENOMEM;
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
    sigset_t_ mask;
    if (user_get(mask_addr, mask))
        return _EFAULT;
    STRACE("sigsuspend(0x%llx) = ...\n", (long long) mask);

    lock(&current->sighand->lock);
    sigmask_set_temp_unlocked(mask);
    while (wait_for(&current->pause, &current->sighand->lock, NULL) != _EINTR)
        continue;
    unlock(&current->sighand->lock);
    STRACE("%d done sigsuspend", current->pid);
    return _EINTR;
}

int_t sys_pause() {
    lock(&current->sighand->lock);
    while (wait_for(&current->pause, &current->sighand->lock, NULL) != _EINTR)
        continue;
    unlock(&current->sighand->lock);
    return _EINTR;
}

int do_kill(pid_t_ pid, dword_t sig, pid_t_ tgid) {
    STRACE("kill(%d, %d)", pid, sig);
    if (sig >= NUM_SIGS)
        return _EINVAL;
    struct siginfo_ info = {
        .code = SI_USER_,
        .kill.pid = current->pid,
        .kill.uid = current->uid,
    };
    // TODO check permissions
    if (pid == 0)
        pid = -current->group->pgid;
    if (pid < 0)
        return send_group_signal(-pid, sig, info);

    lock(&pids_lock);
    struct task *task = pid_get_task(pid);
    if (task == NULL) {
        unlock(&pids_lock);
        return _ESRCH;
    }

    // If tgid is nonzero, it must be correct
    if (tgid != 0 && task->tgid != tgid) {
        unlock(&pids_lock);
        return _ESRCH;
    }

    send_signal(task, sig, info);
    unlock(&pids_lock);
    return 0;
}

dword_t sys_kill(pid_t_ pid, dword_t sig) {
    return do_kill(pid, sig, 0);
}
dword_t sys_tgkill(pid_t_ tgid, pid_t_ tid, dword_t sig) {
    if (tid <= 0 || tgid <= 0)
        return _EINVAL;
    return do_kill(tid, sig, tgid);
}
dword_t sys_tkill(pid_t_ tid, dword_t sig) {
    if (tid <= 0)
        return _EINVAL;
    return do_kill(tid, sig, 0);
}

