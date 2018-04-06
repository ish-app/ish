#include <stdlib.h>
#include "kernel/calls.h"
#include "kernel/task.h"
#include "xX_main_Xx.h"

int main(int argc, char *const argv[]) {
    char *const *envp = NULL;
    if (getenv("TERM"))
        envp = (char *[]) {getenv("TERM") - strlen("TERM") - 1, NULL};
    int err = xX_main_Xx(argc, argv, envp);
    if (err < 0) {
        fprintf(stderr, "%s\n", strerror(-err));
        return err;
    }
    cpu_run(&current->cpu);
}
