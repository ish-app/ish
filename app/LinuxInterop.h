//
//  LinuxInterop.h
//  iSH
//
//  Created by Theodore Dubois on 7/3/21.
//

#ifndef LinuxInterop_h
#define LinuxInterop_h

#ifndef __KERNEL__
#include <sys/types.h>
#else
#include <linux/types.h>
#endif

void actuate_kernel(const char *cmdline);

void async_do_in_irq(void (^block)(void));
void async_do_in_ios(void (^block)(void));
void sync_do_in_ios(void (^block)(void (^done)(void)));

void ReportPanic(const char *message, void (^completion)(void));
void ConsoleLog(const char *data, unsigned len);

typedef const void *nsobj_t;
void objc_put(nsobj_t object);

struct linux_tty {
    struct linux_tty_callbacks *ops;
};
struct linux_tty_callbacks {
    void (*wakeup)(struct linux_tty *tty);
    void (*send_input)(struct linux_tty *tty, const char *data, size_t length);
};

nsobj_t Terminal_terminalWithType_number(int type, int number);
void Terminal_setLinuxTTY(nsobj_t _self, struct linux_tty *tty);
int Terminal_sendOutput_length(nsobj_t _self, const char *data, int size);
int Terminal_roomForOutput(nsobj_t _self);

#endif /* LinuxInterop_h */
