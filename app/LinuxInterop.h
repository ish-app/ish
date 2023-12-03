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
#include <linux/fs.h>
#endif

void async_do_in_irq(void (^block)(void));
void async_do_in_workqueue(void (^block)(void));
void async_do_in_ios(void (^block)(void));
void sync_do_in_workqueue(void (^block)(void (^done)(void)));

// call into ios from kernel:

void actuate_kernel(const char *cmdline);

void ReportPanic(const char *message);
void ConsoleLog(const char *data, unsigned len);
const char *DefaultRootPath(void);

typedef const void *nsobj_t;
nsobj_t objc_get(nsobj_t object);
void objc_put(nsobj_t object);

struct linux_tty {
    struct linux_tty_callbacks *ops;
};
struct linux_tty_callbacks {
    void (*can_output)(struct linux_tty *tty);
    void (*send_input)(struct linux_tty *tty, const char *data, size_t length);
    void (*resize)(struct linux_tty *tty, int cols, int rows);
    void (*hangup)(struct linux_tty *tty);
};

#ifdef __KERNEL__
struct file *ios_pty_open(nsobj_t *terminal_out);
#endif

nsobj_t Terminal_terminalWithType_number(int type, int number);
void Terminal_setLinuxTTY(nsobj_t _self, struct linux_tty *tty);
int Terminal_sendOutput_length(nsobj_t _self, const char *data, int size);
int Terminal_roomForOutput(nsobj_t _self);

nsobj_t UIPasteboard_get(void);
long UIPasteboard_changeCount(void);
void UIPasteboard_set(const char *data, size_t len);
size_t NSData_length(nsobj_t data);
const void *NSData_bytes(nsobj_t data);

// call into kernel from ios:

typedef void (^StartSessionDoneBlock)(int retval, int pid, nsobj_t terminal);
void linux_start_session(const char *exe, const char *const *argv, const char *const *envp, StartSessionDoneBlock done);

void linux_sethostname(const char *hostname);

ssize_t linux_read_file(const char *path, char *buf, size_t size);
ssize_t linux_write_file(const char *path, const char *buf, size_t size);
int linux_remove_directory(const char *path);

#endif /* LinuxInterop_h */
