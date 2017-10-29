#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#include "fs/tty.h"

int mount_root(const struct fs_ops *fs, const char *source);
int create_init_process(const char *program, char *const argv[], char *const envp[]);
int create_stdio(struct tty_driver driver);

#endif
