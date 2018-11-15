// Run a process but replace the vdso with the one in the given file.
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <signal.h>

#include "debug.h"
#include "ptutil.h"
#include "transplant.h"

int main(int argc, char *const argv[]) {
    char *const envp[] = {NULL};
    int pid = start_tracee(AT_FDCWD, argv[2], argv + 2, envp);

    int vdso_fd = trycall(open(argv[1], O_RDONLY), "open vdso");
    struct stat statbuf;
    trycall(fstat(vdso_fd, &statbuf), "stat vdso");
    size_t vdso_size = statbuf.st_size;
    void *vdso = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, vdso_fd, 0);
    if (vdso == MAP_FAILED) {
        perror("mmap vdso"); exit(1);
    }
    transplant_vdso(pid, vdso, vdso_size);

    trycall(kill(pid, SIGSTOP), "pause process");
    printf("attach debugger to %d\n", pid);
    return 0;
}
