#include <unistd.h>
#include "kernel/calls.h"
#include "debug.h"

static fd_t pipe_f_create(int pipe_fd) {
    fd_t f = fd_next();
    if (f == -1)
        return _EMFILE;
    struct fd *fd = adhoc_fd_create();
    if (fd == NULL)
        return _ENOMEM;
    fd->real_fd = pipe_fd;
    fd->ops = &realfs_fdops;
    current->files[f] = fd;
    return f;
}

dword_t sys_pipe(addr_t pipe_addr) {
    STRACE("pipe(0x%x)", pipe_addr);
    int p[2];
    int err = pipe(p);
    if (err < 0)
        return err;

    int fp[2];
    err = fp[0] = pipe_f_create(p[0]);
    if (fp[0] < 0)
        goto close_pipe;
    err = fp[1] = pipe_f_create(p[1]);
    if (fp[1] < 0)
        goto close_fake_0;

    err = _EFAULT;
    if (user_put(pipe_addr, fp))
        goto close_fake_1;
    return 0;

close_fake_1:
    sys_close(fp[1]);
close_fake_0:
    sys_close(fp[0]);
close_pipe:
    close(p[0]);
    close(p[1]);
    return err;
}
