//
//  LinuxInterop.c
//  iSH
//
//  Created by Theodore Dubois on 7/3/21.
//

#include "LinuxInterop.h"
#include <linux/start_kernel.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/string.h>
#include <linux/completion.h>

extern void run_kernel(void);

void actuate_kernel(const char *cmdline) {
    strcpy(boot_command_line, cmdline);
    run_kernel();
}

static int panic_report(struct notifier_block *nb, unsigned long action, void *data) {
    const char *message = data;
    DECLARE_COMPLETION(panic_report_done);
    ReportPanic(message, ^{
        // TODO: complete(&panic_report_done); in irq
    });
    wait_for_completion(&panic_report_done);
    return 0;
}

static struct notifier_block panic_report_block = {
    .notifier_call = panic_report,
    .priority = INT_MAX,
};
static int __init panic_report_init(void) {
    atomic_notifier_chain_register(&panic_notifier_list, &panic_report_block);
    return 0;
}
__initcall(panic_report_init);
