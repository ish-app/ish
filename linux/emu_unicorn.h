#ifndef __UNICORN_UNICORN_H
#define __UNICORN_UNICORN_H
#if __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#endif

struct emu_uc {
	struct uc_struct *uc;
	unsigned long tls_ptr;
	unsigned long mm_flush_count;
};

extern bool unicorn_trace;

#endif
