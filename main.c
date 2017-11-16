#include <stdlib.h>
#include "kernel/calls.h"
#include "kernel/process.h"
#include "xX_main_Xx.h"

int main(int argc, char *const argv[]) {
    int err = xX_main_Xx(argc, argv);
    if (err < 0) {
        fprintf(stderr, "%s\n", strerror(-err));
        return err;
    }
    cpu_run(&current->cpu);
}
