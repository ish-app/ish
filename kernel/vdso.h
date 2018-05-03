#ifndef KERNEL_VDSO_H
#define KERNEL_VDSO_H

extern const char vdso_data[8192];
int vdso_symbol(const char *name);

#endif
