#include "ptrace.h"
#include "util/list.h"
#include "task.h"
#include "kernel/errno.h"

dword_t sys_ptrace(dword_t request, dword_t pid, addr_t addr, dword_t data) {
    static int i;
    struct task *child = NULL;
    switch (request) {
        case PTRACE_TRACEME_:
            current->traced = true;
            break;
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
            break;
        default:
            return _EPERM;
    }
}
