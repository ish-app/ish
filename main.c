#include <stdlib.h>
#include "sys/calls.h"
#include "emu/process.h"

int main(int argc, char *const args[]) {
    int err;
    current = process_create();
    char *const argv[] = {args[1], NULL};
    char *const envp[] = {NULL};
    if ((err = sys_execve(args[1], argv, envp)) < 0) {
        return -err;
    }
    cpu_run(&current->cpu);
}
