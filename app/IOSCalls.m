//
//  IOSCalls.m
//  libiSHApp
//
//  Created by Theodore Dubois on 8/15/21.
//

#if ISH_LINUX

#include <Foundation/Foundation.h>
#include "LinuxInterop.h"

void async_do_in_ios(void (^block)(void)) {
    dispatch_async(dispatch_get_main_queue(), block);
}

void ConsoleLog(const char *data, unsigned len) {
    NSLog(@"%.*s", len, data);
}

void objc_put(nsobj_t object) {
    CFBridgingRelease(object);
}

#endif
