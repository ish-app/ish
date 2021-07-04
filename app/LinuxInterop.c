//
//  LinuxInterop.c
//  iSH
//
//  Created by Theodore Dubois on 7/3/21.
//

#include <linux/start_kernel.h>
#include <string.h>

extern void run_kernel(void);

void actuate_kernel(char *cmdline) {
    strcpy(boot_command_line, cmdline);
    run_kernel();
}
