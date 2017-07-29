#include "sys/calls.h"

dword_t sys_getpid() {
    return current->pid;
}
dword_t sys_gettid() {
    return current->pid;
}
dword_t sys_getppid() {
    return current->ppid;
}

dword_t sys_getuid32() {
    return current->uid;
}
dword_t sys_getuid() {
    return current->uid & 0xffff;
}

dword_t sys_geteuid32() {
    return current->euid;
}
dword_t sys_geteuid() {
    return current->euid & 0xffff;
}

dword_t sys_getgid32() {
    return current->gid;
}
dword_t sys_getgid() {
    return current->gid & 0xffff;
}
