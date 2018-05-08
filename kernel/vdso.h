#ifndef KERNEL_VDSO_H
#define KERNEL_VDSO_H

extern const char vdso_data[8192] __asm__("vdso_data");
int vdso_symbol(const char *name);

#endif
