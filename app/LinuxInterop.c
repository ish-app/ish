//
//  LinuxInterop.c
//  iSH
//
//  Created by Theodore Dubois on 7/3/21.
//

#include "LinuxInterop.h"
#include <Block.h>
#include <linux/start_kernel.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <user/fs.h>
#include <user/irq.h>

extern void run_kernel(void);

void actuate_kernel(const char *cmdline) {
    strcpy(boot_command_line, cmdline);
    run_kernel();
}

void sync_do_in_ios(void (^block)(void (^done)(void))) {
    DECLARE_COMPLETION(panic_report_done);
    struct completion *done_ptr = &panic_report_done;
    async_do_in_ios(^{
        block(^{
            async_do_in_irq(^{
                complete(done_ptr);
            });
        });
    });
    wait_for_completion(done_ptr);
}

static int panic_report(struct notifier_block *nb, unsigned long action, void *data) {
    const char *message = data;
    sync_do_in_ios(^(void (^done)(void)) {
        ReportPanic(message, done);
    });
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
core_initcall(panic_report_init);

static int block_request_read;
static int block_request_write;
static irqreturn_t call_block_irq(int irq, void *dev) {
    void (^block)(void);
    for (;;) {
        int err = host_read(block_request_read, &block, sizeof(block));
        if (err <= 0)
            break;
        block();
        Block_release(block);
    }
    return IRQ_HANDLED;
}

void async_do_in_irq(void (^block)(void)) {
    block = Block_copy(block);
    int err = host_write(block_request_write, &block, sizeof(block));
    if (err < 0)
        __builtin_trap();
    trigger_irq(CALL_BLOCK_IRQ);
}

static int __init call_block_init(void) {
    int err = host_pipe(&block_request_read, &block_request_write);
    if (err < 0)
        return err;
    err = fd_set_nonblock(block_request_read);
    if (err < 0)
        return err;
    err = request_irq(CALL_BLOCK_IRQ, call_block_irq, 0, "block", NULL);
    if (err < 0)
        return err;
    return 0;
}
subsys_initcall(call_block_init);
