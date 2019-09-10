#include <string.h>
#include "ptrace.h"
#include "util/list.h"
#include "task.h"
#include "kernel/errno.h"
#include "calls.h"

dword_t sys_ptrace(dword_t request, dword_t pid, addr_t addr, dword_t data) {
    STRACE("ptrace(%d, %d, 0x%x, 0x%x)", request, pid, addr, data);
    struct task *child = NULL;
    switch (request) {
        case PTRACE_TRACEME_:
            current->traced = true;
            return 0;
        case PTRACE_SINGLESTEP_:
            list_for_each_entry(&current->children, child, siblings) {
                if (child->pid == pid) {
                    break;
                }
            }
            if (child) {
                lock(&child->cpu.step_lock);
                child->cpu.should_step = true;
                notify(&child->cpu.step_cond);
                lock(&child->group->lock);
                child->group->stopped = false;
                notify(&child->group->stopped_cond);
                unlock(&child->group->lock);
                unlock(&child->cpu.step_lock);
            } else {
                return _EPERM;
            }
            return 0;
        case PTRACE_GETREGS_:
            list_for_each_entry(&current->children, child, siblings) {
                if (child->pid == pid) {
                    break;
                }
            }
            if (child) {
                struct user_regs_struct_ user_regs_ = {0};
                user_regs_.ebx = child->cpu.ebx;
                user_regs_.ecx = child->cpu.ecx;
                user_regs_.edx = child->cpu.edx;
                user_regs_.esi = child->cpu.esi;
                user_regs_.edi = child->cpu.edi;
                user_regs_.ebp = child->cpu.ebp;
                user_regs_.eax = child->cpu.eax;
//                user_regs_.xds = child->cpu.xds;
//                user_regs_.xes = child->cpu.xes;
//                user_regs_.xfs = child->cpu.xfs;
//                user_regs_.xgs = child->cpu.xgs;
                user_regs_.orig_eax = child->cpu.eax;
                user_regs_.eip = child->cpu.eip;
//                user_regs_.xcs = child->cpu.xcs;
                user_regs_.eflags = child->cpu.eflags;
                user_regs_.esp = child->cpu.esp;
//                user_regs_.xss = child->cpu.xss;
                if (data && user_put(data, user_regs_)) {
                    return _EFAULT;
                }
            } else {
                return _EPERM;
            }
            return 0;
        default:
            return _EPERM;
    }
}
