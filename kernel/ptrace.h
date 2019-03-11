#ifndef PTRACE_h
#define PTRACE_h
#include "misc.h"

#define PTRACE_TRACEME_ 0
#define PTRACE_SINGLESTEP_ 9

// TODO: the types are probably wrong
dword_t sys_ptrace(dword_t request, dword_t pid, addr_t addr, dword_t data);

#endif /* ptrace_h */
