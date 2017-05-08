#include <pthread.h>
#include "sys/calls.h"

int sys_exit(dword_t status) {
    // TODO free current task structures
    pthread_exit(NULL);
}
