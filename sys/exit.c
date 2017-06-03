#include <pthread.h>
#include "sys/calls.h"

dword_t sys_exit(dword_t status) {
    // TODO free current task structures
    pthread_exit(NULL);
}
