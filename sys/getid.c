#include "sys/calls.h"
#include "emu/process.h"

dword_t sys_getuid32() {
    return current->uid;
}
dword_t sys_getuid() {
    return current->uid & 0xffff;
}

dword_t sys_getgid32() {
    return current->gid;
}
dword_t sys_getgid() {
    return current->gid & 0xffff;
}
