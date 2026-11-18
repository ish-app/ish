#include "ptrace.h"
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "kernel/signal.h"
#include "task.h"
#include <string.h>

// Returns stopped child with the given pid, locked with the ptrace lock
static struct task *find_child(pid_t_ pid) {
    struct task *child = NULL;
    list_for_each_entry(&current->children, child, siblings) {
        if (child->pid == pid) {
            lock(&child->ptrace.lock);
            if (child->ptrace.stopped) {
                goto found;
            }

            unlock(&child->ptrace.lock);
        }
    }
    child = NULL;
found:
    return child;
}

// Ensure stopped, ptrace locked, etc. before calling this
#if ISH_GUEST_64
static void get_user_regs(struct cpu_state *cpu, struct user_regs_struct_ *r) {
    r->r15 = cpu->r15; r->r14 = cpu->r14; r->r13 = cpu->r13; r->r12 = cpu->r12;
    r->rbp = cpu->rbp; r->rbx = cpu->rbx; r->r11 = cpu->r11; r->r10 = cpu->r10;
    r->r9 = cpu->r9; r->r8 = cpu->r8; r->rax = cpu->rax; r->rcx = cpu->rcx;
    r->rdx = cpu->rdx; r->rsi = cpu->rsi; r->rdi = cpu->rdi;
    r->orig_rax = cpu->rax;
    r->rip = cpu->rip;
    r->cs = 0x33;
    r->eflags = cpu->eflags;
    r->rsp = cpu->rsp;
    r->ss = 0x2b;
    r->fs_base = cpu->fs_base;
    r->gs_base = cpu->gs_base;
}

static void set_user_regs(struct cpu_state *cpu, struct user_regs_struct_ *r) {
    cpu->r15 = r->r15; cpu->r14 = r->r14; cpu->r13 = r->r13; cpu->r12 = r->r12;
    cpu->rbp = r->rbp; cpu->rbx = r->rbx; cpu->r11 = r->r11; cpu->r10 = r->r10;
    cpu->r9 = r->r9; cpu->r8 = r->r8; cpu->rax = r->rax; cpu->rcx = r->rcx;
    cpu->rdx = r->rdx; cpu->rsi = r->rsi; cpu->rdi = r->rdi;
    cpu->rip = r->rip;
    // only the arithmetic flags and the direction flag belong to the tracee
    cpu->eflags = (cpu->eflags & ~0xcd5u) | ((dword_t) r->eflags & 0xcd5u);
    cpu->cf = cpu->cf_bit;
    cpu->of = cpu->of_bit;
    cpu->flags_res = 0;
    cpu->rsp = r->rsp;
    cpu->fs_base = r->fs_base;
    cpu->gs_base = r->gs_base;
}

static void get_user_fpregs(struct cpu_state *cpu, struct user_fpregs_struct_ *fp) {
    fp->cwd = cpu->fcw;
    fp->swd = cpu->fsw;
    fp->ftw = 0xff;
    fp->mxcsr = cpu->mxcsr;
    fp->mxcr_mask = 0xffff;
    for (int i = 0; i < 8; i++)
        memcpy(fp->st_space[i], &cpu->fp[(cpu->top + i) & 7], 10);
    memcpy(fp->xmm_space, cpu->xmm, sizeof(fp->xmm_space));
}

static void set_user_fpregs(struct cpu_state *cpu, struct user_fpregs_struct_ *fp) {
    cpu->fcw = fp->cwd;
    cpu->fsw = fp->swd;
    cpu->mxcsr = fp->mxcsr & 0xffff;
    for (int i = 0; i < 8; i++)
        memcpy(&cpu->fp[(cpu->top + i) & 7], fp->st_space[i], 10);
    memcpy(cpu->xmm, fp->xmm_space, sizeof(fp->xmm_space));
}
#else
static void get_user_regs(struct cpu_state *cpu, struct user_regs_struct_ *user_regs_) {
    user_regs_->ebx = cpu->ebx;
    user_regs_->ecx = cpu->ecx;
    user_regs_->edx = cpu->edx;
    user_regs_->esi = cpu->esi;
    user_regs_->edi = cpu->edi;
    user_regs_->ebp = cpu->ebp;
    user_regs_->eax = cpu->eax;
    user_regs_->orig_eax = cpu->eax;
    user_regs_->eip = cpu->eip;
    user_regs_->eflags = cpu->eflags;
    user_regs_->esp = cpu->esp;
}

static void set_user_regs(struct cpu_state *cpu, struct user_regs_struct_ *user_regs_) {
    cpu->ebx = user_regs_->ebx;
    cpu->ecx = user_regs_->ecx;
    cpu->edx = user_regs_->edx;
    cpu->esi = user_regs_->esi;
    cpu->edi = user_regs_->edi;
    cpu->ebp = user_regs_->ebp;
    cpu->eax = user_regs_->eax;
    cpu->eip = user_regs_->eip;
    cpu->eflags = user_regs_->eflags;
    cpu->esp = user_regs_->esp;
}

// TODO floating point registers for the 32-bit guest
static void get_user_fpregs(struct cpu_state *cpu, struct user_fpregs_struct_ *fp) {
    (void) cpu; (void) fp;
}
static void set_user_fpregs(struct cpu_state *cpu, struct user_fpregs_struct_ *fp) {
    (void) cpu; (void) fp;
}
#endif

dword_t sys_ptrace(dword_t request, dword_t pid, addr_t addr, uword_t data) {
    switch (request) {
        case PTRACE_TRACEME_:
            STRACE("ptrace(PTRACE_TRACEME, %d, %#x, %#x)", pid, addr, data);
            current->ptrace.traced = true;
            return 0;

        case PTRACE_PEEKTEXT_:
        case PTRACE_PEEKDATA_: {
            STRACE("ptrace(PTRACE_PEEKDATA, %d, %#x, %#x)", pid, addr, data);
            uword_t peek;
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            if (user_get_task(child, addr, peek)) {
                unlock(&child->ptrace.lock);
                return _EFAULT;
            } else if (user_put(data, peek)) {
                unlock(&child->ptrace.lock);
                return _EFAULT;
            }
            unlock(&child->ptrace.lock);

            return 0;
        }

        case PTRACE_PEEKUSER_: {
            STRACE("ptrace(PTRACE_PEEKUSER, %d, %#x, %#x)", pid, addr, data);
            uword_t peek;
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            struct user_ user_ = {};
            get_user_regs(&child->cpu, &user_.user_regs);

            if (addr & (sizeof(peek) - 1) || addr >= sizeof(struct user_)) {
                unlock(&child->ptrace.lock);
                return _EIO;
            }

            memcpy(&peek, (char *)&user_ + addr, sizeof(peek));
            if (user_put(data, peek)) {
                unlock(&child->ptrace.lock);
                return _EFAULT;
            }
            unlock(&child->ptrace.lock);

            return 0;
        }

        case PTRACE_POKETEXT_:
        case PTRACE_POKEDATA_: {
            STRACE("ptrace(PTRACE_POKEDATA, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            if (user_write_task_ptrace(child, addr, &data, sizeof(data))) {
                unlock(&child->ptrace.lock);
                return _EFAULT;
            }
            unlock(&child->ptrace.lock);

            return 0;
        }

        case PTRACE_CONT_: {
            STRACE("ptrace(PTRACE_CONT, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            child->cpu.tf = false;
            child->ptrace.stopped = false;
            notify(&child->ptrace.cond);
            unlock(&child->ptrace.lock);

            return 0;
        }

        case PTRACE_KILL_: {
            STRACE("ptrace(PTRACE_KILL, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            child->ptrace.stopped = false;
            send_signal(child, SIGKILL_, SIGINFO_NIL);
            unlock(&child->ptrace.lock);

            return 0;
        }

        case PTRACE_SINGLESTEP_: {
            STRACE("ptrace(PTRACE_SINGLESTEP, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            child->cpu.tf = true;
            child->ptrace.stopped = false;
            notify(&child->ptrace.cond);
            unlock(&child->ptrace.lock);

            return 0;
        }

        case PTRACE_GETREGS_: {
            STRACE("ptrace(PTRACE_GETREGS, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            struct user_regs_struct_ user_regs_ = {};
            get_user_regs(&child->cpu, &user_regs_);
            if (user_put(data, user_regs_)) {
                unlock(&child->ptrace.lock);
                return _EFAULT;
            }
            unlock(&child->ptrace.lock);

            return 0;
        }

        case PTRACE_SETREGS_: {
            STRACE("ptrace(PTRACE_SETREGS, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            struct user_regs_struct_ user_regs_;
            if (user_get(data, user_regs_)) {
                unlock(&child->ptrace.lock);
                return _EFAULT;
            }
            set_user_regs(&child->cpu, &user_regs_);
            unlock(&child->ptrace.lock);

            return 0;
        }

        // GDB needs the fpregs functions to exist if you want to evaluate things
        case PTRACE_GETFPREGS_: {
            STRACE("ptrace(PTRACE_GETFPREGS, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            struct user_fpregs_struct_ user_fpregs_ = {};
            get_user_fpregs(&child->cpu, &user_fpregs_);
            if (user_put(data, user_fpregs_)) {
                unlock(&child->ptrace.lock);
                return _EFAULT;
            }
            unlock(&child->ptrace.lock);

            return 0;
        }

        case PTRACE_SETFPREGS_: {
            STRACE("ptrace(PTRACE_SETFPREGS, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            struct user_fpregs_struct_ user_fpregs_;
            if (user_get(data, user_fpregs_)) {
                unlock(&child->ptrace.lock);
                return _EFAULT;
            }
            set_user_fpregs(&child->cpu, &user_fpregs_);
            unlock(&child->ptrace.lock);

            return 0;
        }

        case PTRACE_SETOPTIONS_:
            STRACE("ptrace(PTRACE_SETOPTIONS, %d, %#x, %#x)", pid, addr, data);
            return _EINVAL;

        case PTRACE_GETSIGINFO_: {
            STRACE("ptrace(PTRACE_GETSIGINFO, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            if (data && user_put(data, child->ptrace.info)) {
                unlock(&child->ptrace.lock);
                return _EFAULT;
            }
            unlock(&child->ptrace.lock);

            return 0;
        }

        default:
            STRACE("ptrace(%d, %d, %#x, %#x)", request, pid, addr, data);
            return _EPERM;
    }
}
