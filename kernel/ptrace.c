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
static void get_user_regs(struct cpu_state *cpu, struct user_regs_struct_ *user_regs_) {
    user_regs_->ebx = cpu->ebx;
    user_regs_->ecx = cpu->ecx;
    user_regs_->edx = cpu->edx;
    user_regs_->esi = cpu->esi;
    user_regs_->edi = cpu->edi;
    user_regs_->ebp = cpu->ebp;
    user_regs_->eax = cpu->eax;
//  user_regs_->xds = cpu->xds;
//  user_regs_->xes = cpu->xes;
//  user_regs_->xfs = cpu->xfs;
//  user_regs_->xgs = cpu->xgs;
    user_regs_->orig_eax = cpu->eax;
    user_regs_->eip = cpu->eip;
//  user_regs_->xcs = cpu->xcs;
    user_regs_->eflags = cpu->eflags;
    user_regs_->esp = cpu->esp;
//  user_regs_->xss = cpu->xss;
}

// Ensure stopped, ptrace locked, etc. before calling this
static void set_user_regs(struct cpu_state *cpu, struct user_regs_struct_ *user_regs_) {
    cpu->ebx = user_regs_->ebx;
    cpu->ecx = user_regs_->ecx;
    cpu->edx = user_regs_->edx;
    cpu->esi = user_regs_->esi;
    cpu->edi = user_regs_->edi;
    cpu->ebp = user_regs_->ebp;
    cpu->eax = user_regs_->eax;
//  cpu->xds = user_regs_->xds;
//  cpu->xes = user_regs_->xes;
//  cpu->xfs = user_regs_->xfs;
//  cpu->xgs = user_regs_->xgs;
//  cpu->eax = user_regs_->orig_eax;
    cpu->eip = user_regs_->eip;
//  cpu->xcs = user_regs_->xcs;
    cpu->eflags = user_regs_->eflags;
    cpu->esp = user_regs_->esp;
//  cpu->xss = user_regs_->xss;
}

dword_t sys_ptrace(dword_t request, dword_t pid, addr_t addr, dword_t data) {
    switch (request) {
        case PTRACE_TRACEME_:
            STRACE("ptrace(PTRACE_TRACEME, %d, %#x, %#x)", pid, addr, data);
            current->ptrace.traced = true;
            return 0;

        case PTRACE_PEEKTEXT_:
        case PTRACE_PEEKDATA_: {
            STRACE("ptrace(PTRACE_PEEKDATA, %d, %#x, %#x)", pid, addr, data);
            dword_t peek;
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
            dword_t peek;
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            struct user_ user_ = {};
            get_user_regs(&child->cpu, &user_.user_regs);

            if (addr & (sizeof(peek) - 1) || addr >= sizeof(struct user_))
                return _EIO;

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
                return _EFAULT;
            } else {
                set_user_regs(&child->cpu, &user_regs_);
            }
            unlock(&child->ptrace.lock);

            return 0;
        }

        // GDB needs the fpregs functions to exist if you want to evaluate things
        case PTRACE_GETFPREGS_: {
            STRACE("ptrace(PTRACE_GETFPREGS, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            struct user_fpregs_struct_ user_fpregs_ = {};
            if (user_put(data, user_fpregs_)) {
                unlock(&child->ptrace.lock);
                return _EFAULT;
            }
            // TODO get float point registers
            unlock(&child->ptrace.lock);

            return 0;
        }

        case PTRACE_SETFPREGS_: {
            STRACE("ptrace(PTRACE_SETFPREGS, %d, %#x, %#x)", pid, addr, data);
            struct task *child = find_child(pid);
            if (!child) return _EPERM;

            struct user_fpregs_struct_ user_fpregs_;
            if (user_get(data, user_fpregs_)) {
                return _EFAULT;
            } else {
                // TODO set floating point registers
            }
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
