#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#include "fs/tty.h"

int mount_root(const struct fs_ops *fs, const char *source);
void create_first_process(void);
void set_console_device(int major, int minor);
int create_stdio(const char *file, struct tty_driver *driver);

#endif
