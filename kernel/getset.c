#include "kernel/calls.h"

dword_t sys_getpid() {
    STRACE("getpid()");
    return current->pid;
}
dword_t sys_gettid() {
    STRACE("gettid()");
    return current->pid;
}
dword_t sys_getppid() {
    STRACE("getppid()");
    pid_t_ ppid;
    lock(&pids_lock);
    if (current->parent != NULL)
        ppid = current->parent->pid;
    else
        ppid = 0;
    unlock(&pids_lock);
    return ppid;
}
dword_t sys_getpgid(dword_t pid) {
    STRACE("getpgid(%d)", pid);
    if (pid != 0)
        return _EPERM;
    return current->pgid;
}
dword_t sys_getpgrp() {
    STRACE("getpgrp()");
    return current->pgid;
}

dword_t sys_getuid32() {
    STRACE("getuid32()");
    return current->uid;
}
dword_t sys_getuid() {
    STRACE("getuid()");
    return current->uid & 0xffff;
}

dword_t sys_geteuid32() {
    STRACE("geteuid32()");
    return current->euid;
}
dword_t sys_geteuid() {
    STRACE("geteuid()");
    return current->euid & 0xffff;
}

dword_t sys_getgid32() {
    STRACE("getgid32()");
    return current->gid;
}
dword_t sys_getgid() {
    STRACE("getgid()");
    return current->gid & 0xffff;
}

dword_t sys_getegid32() {
    STRACE("getegid32()");
    return current->egid;
}
dword_t sys_getegid() {
    STRACE("getegid()");
    return current->egid & 0xffff;
}

