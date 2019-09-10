#ifndef PTRACE_h
#define PTRACE_h
#include "misc.h"

#define PTRACE_TRACEME_ 0
#define PTRACE_SINGLESTEP_ 9
#define PTRACE_GETREGS_ 12

struct user_regs_struct_ {
    dword_t ebx;
    dword_t ecx;
    dword_t edx;
    dword_t esi;
    dword_t edi;
    dword_t ebp;
    dword_t eax;
    dword_t xds;
    dword_t xes;
    dword_t xfs;
    dword_t xgs;
    dword_t orig_eax;
    dword_t eip;
    dword_t xcs;
    dword_t eflags;
    dword_t esp;
    dword_t xss;
};

// TODO: the types are probably wrong
dword_t sys_ptrace(dword_t request, dword_t pid, addr_t addr, dword_t data);

#endif /* ptrace_h */
