#include <linux/compiler_attributes.h>
#include <linux/start_kernel.h>
#include <linux/string.h>
#include <user/user.h>

extern void run_kernel(void);

int main(int argc, const char *argv[])
{
	int i;
	for (i = 1; i < argc; i++) {
		if (i > 1)
			strcat(boot_command_line, " ");
		strcat(boot_command_line, argv[i]);
	}
	run_kernel();
	for (;;) host_pause();
	return 0;
}
