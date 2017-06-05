#include <pthread.h>
#include "sys/calls.h"

dword_t sys_exit(dword_t status) {
    // TODO free current task structures
    pthread_exit(NULL);
}

dword_t sys_exit_group(dword_t status) {
    pthread_exit(NULL);
}
