#include <stdlib.h>
#include "sys/calls.h"
#include "emu/process.h"

int main(int argc, const char *argv[]) {
    int err;
    current = process_create();
    if ((err = sys_execve(argv[1], NULL, NULL)) < 0) {
        return -err;
    }
    cpu_run(&current->cpu);
}
