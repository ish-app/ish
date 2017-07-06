#include "emu/process.h"
#include "sys/fs.h"

void setup() {
    // god help us
    current = process_create();
    current->ppid = 1;
    current->uid = current->gid = 0;
    mount_root();
    current->pwd = getcwd(NULL, 0);
    current->files[0] = malloc(sizeof(struct fd));
    current->files[0]->ops = &realfs_fdops;
    current->files[0]->real_fd = 0;
    current->files[0]->refcnt = 1;
    current->files[1] = malloc(sizeof(struct fd));
    current->files[1]->ops = &realfs_fdops;
    current->files[1]->real_fd = 1;
    current->files[1]->refcnt = 1;
    current->files[2] = malloc(sizeof(struct fd));
    current->files[2]->ops = &realfs_fdops;
    current->files[2]->real_fd = 2;
    current->files[2]->refcnt = 1;
}
