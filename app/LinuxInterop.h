//
//  LinuxInterop.h
//  iSH
//
//  Created by Theodore Dubois on 7/3/21.
//

#ifndef LinuxInterop_h
#define LinuxInterop_h

void call_in_irq(void (^block)(void));

void ReportPanic(const char *message, void (^completion)(void));

void actuate_kernel(const char *cmdline);

#endif /* LinuxInterop_h */
