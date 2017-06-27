#include "sys/calls.h"
#include "sys/signal.h"

int xsave_extra = 0;
int fxsave_extra = 0;

int send_signal(struct process *proc, int sig) {
    if (proc->sigactions[sig].handler == SIG_IGN_)
        return 0;
    if (proc->sigactions[sig].handler == SIG_DFL_)
        sys_exit(0);

    proc->cpu.eax = sig;
    proc->cpu.eip = proc->sigactions[sig].handler;

    dword_t sp = proc->cpu.esp;
    if (xsave_extra) {
        // do as the kernel does
        // this is superhypermega condensed version of fpu__alloc_mathframe in
        // arch/x86/kernel/fpu/signal.c
        sp -= xsave_extra;
        debugger;
        sp &=~ 0x3f;
        sp -= fxsave_extra;
    }
    sp -= sizeof(struct sigframe_);
    // align sp + 4 on a 16-byte boundary because that's what the abi says
	sp = ((sp + 4) & ~0xf) - 4;
    proc->cpu.esp = sp;
    return 0;
}

static int do_sigaction(int sig, const struct sigaction_ *action, struct sigaction_ *oldaction) {
    if (sig >= NUM_SIGS)
        return _EINVAL;
    if (sig == SIGKILL_ || sig == SIGSTOP_)
        return _EINVAL;

    *oldaction = current->sigactions[sig];
    current->sigactions[sig] = *action;
    return 0;
}

dword_t sys_rt_sigaction(dword_t signum, addr_t action_addr, addr_t oldaction_addr, dword_t sigset_size) {
    if (sigset_size != sizeof(sigset_t_))
        return _EINVAL;
    struct sigaction_ action, oldaction;
    user_get_count(action_addr, &action, sizeof(action));
    if (oldaction_addr != 0)
        user_get_count(oldaction_addr, &oldaction, sizeof(oldaction));

    int err = do_sigaction(signum, &action, &oldaction);
    if (err < 0)
        return err;

    if (oldaction_addr != 0)
        user_put_count(oldaction_addr, &oldaction, sizeof(oldaction));
    return err;
}

dword_t sys_sigaction(dword_t signum, addr_t action_addr, addr_t oldaction_addr) {
    return sys_rt_sigaction(signum, action_addr, oldaction_addr, 1);
}
