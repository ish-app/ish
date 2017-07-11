#include <stdlib.h>
#include "sys/calls.h"
#include "sys/process.h"

int main(int argc, char *const argv[]) {
    int err;
    setup();
    char *envp[] = {NULL};
    if ((err = sys_execve(argv[1], argv + 1, envp)) < 0) {
        return -err;
    }
    cpu_run(&current->cpu);
}
