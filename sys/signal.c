#include "sys/calls.h"
#include "sys/signal.h"
#include "sys/vdso.h"

int xsave_extra = 0;
int fxsave_extra = 0;

void send_signal(struct process *proc, int sig) {
    if (proc->sigactions[sig].handler == SIG_IGN_)
        return;
    if (proc->sigactions[sig].handler == SIG_DFL_)
        sys_exit(0);

    // setup the frame
    struct sigframe_ frame = {};
    frame.sig = sig;

    struct cpu_state *cpu = &proc->cpu;
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

    // TODO get address of sigreturn from vdso symbol table
    addr_t sigreturn_addr = vdso_symbol("__kernel_rt_sigreturn");
    if (sigreturn_addr == 0) {
        fprintf(stderr, "sigreturn not found in vdso, this should never happen\n");
        abort();
    }
    frame.pretcode = proc->vdso + sigreturn_addr;
    // for legacy purposes
    frame.retcode.popmov = 0xb858;
    frame.retcode.nr_sigreturn = 173; // rt_sigreturn
    frame.retcode.int80 = 0x80cd;

    // set up registers for signal handler
    cpu->eax = sig;
    cpu->eip = proc->sigactions[sig].handler;

    dword_t sp = cpu->esp;
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
    cpu->esp = sp;

    // install frame
    user_put_count(sp, &frame, sizeof(frame));

    return;
}

static int do_sigaction(int sig, const struct sigaction_ *action, struct sigaction_ *oldaction) {
    if (sig >= NUM_SIGS)
        return _EINVAL;
    if (sig == SIGKILL_ || sig == SIGSTOP_)
        return _EINVAL;

    if (oldaction)
        *oldaction = current->sigactions[sig];
    if (action)
        current->sigactions[sig] = *action;
    return 0;
}

dword_t sys_rt_sigaction(dword_t signum, addr_t action_addr, addr_t oldaction_addr, dword_t sigset_size) {
    if (sigset_size != sizeof(sigset_t_))
        return _EINVAL;
    struct sigaction_ action, oldaction;
    if (action_addr != 0)
        user_get_count(action_addr, &action, sizeof(action));
    if (oldaction_addr != 0)
        user_get_count(oldaction_addr, &oldaction, sizeof(oldaction));

    int err = do_sigaction(signum,
            action_addr ? &action : NULL,
            oldaction_addr ? &oldaction : NULL);
    if (err < 0)
        return err;

    if (oldaction_addr != 0)
        user_put_count(oldaction_addr, &oldaction, sizeof(oldaction));
    return err;
}

dword_t sys_sigaction(dword_t signum, addr_t action_addr, addr_t oldaction_addr) {
    return sys_rt_sigaction(signum, action_addr, oldaction_addr, 1);
}

#define FAKE_SIG_UNBLOCK 1

dword_t sys_rt_sigprocmask(dword_t how, addr_t set, addr_t oldset, dword_t size) {
    // only allow musl's funky thing it does in sigaction
    if (how != FAKE_SIG_UNBLOCK || oldset != 0 || size != 8)
        TODO("rt_sigprocmask should actually work");
    return 0;
}
