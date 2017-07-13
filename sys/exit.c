#include <pthread.h>
#include "sys/calls.h"

noreturn void do_exit(int status) {
    // TODO free current task structures
    printf("pid %d exit status %d\n", current->pid, status);
    pthread_exit(NULL);
}

noreturn dword_t sys_exit(dword_t status) {
    do_exit(status << 8);
}

dword_t sys_exit_group(dword_t status) {
    sys_exit(status);
}
