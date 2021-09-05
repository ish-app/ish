//
//  LinuxInterop.h
//  iSH
//
//  Created by Theodore Dubois on 7/3/21.
//

#ifndef LinuxInterop_h
#define LinuxInterop_h

void async_do_in_irq(void (^block)(void));
void async_do_in_ios(void (^block)(void));
void sync_do_in_ios(void (^block)(void (^done)(void)));

void ReportPanic(const char *message, void (^completion)(void));
void ConsoleLog(const char *data, unsigned len);

typedef const void *nsobj_t;
void objc_put(nsobj_t object);
nsobj_t Terminal_terminalWithType_number(int type, int number);
int Terminal_sendOutput_length(nsobj_t _self, const char *data, int size);
int Terminal_roomForOutput(nsobj_t _self);

void actuate_kernel(const char *cmdline);

#endif /* LinuxInterop_h */
