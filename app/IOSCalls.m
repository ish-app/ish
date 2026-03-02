//
//  IOSCalls.m
//  libiSHApp
//
//  Created by Theodore Dubois on 8/15/21.
//

#if ISH_LINUX

#include <UIKit/UIKit.h>
#include <pthread.h>
#include "Roots.h"
#include "LinuxInterop.h"

void async_do_in_ios(void (^block)(void)) {
    dispatch_async(dispatch_get_main_queue(), block);
}

void ConsoleLog(const char *data, unsigned len) {
    NSLog(@"%.*s", len, data);
}

nsobj_t objc_get(nsobj_t object) {
    CFBridgingRetain((__bridge id) object);
    return object;
}

void objc_put(nsobj_t object) {
    CFBridgingRelease(object);
}

void sync_do_in_workqueue(void (^block)(void (^done)(void))) {
    __block pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    __block pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    __block bool flag = false;
    async_do_in_workqueue(^{
        block(^{
            pthread_mutex_lock(&mutex);
            flag = true;
            pthread_mutex_unlock(&mutex);
            pthread_cond_broadcast(&cond);
        });
    });
    pthread_mutex_lock(&mutex);
    while (!flag)
        pthread_cond_wait(&cond, &mutex);
    pthread_mutex_unlock(&mutex);
}

long UIPasteboard_changeCount(void) {
    return UIPasteboard.generalPasteboard.changeCount;
}
nsobj_t UIPasteboard_get(void) {
    return (__bridge nsobj_t) [UIPasteboard.generalPasteboard.string dataUsingEncoding:NSUTF8StringEncoding];
}
void UIPasteboard_set(const char *data, size_t len) {
    UIPasteboard.generalPasteboard.string = [[NSString alloc] initWithBytes:data length:len encoding:NSUTF8StringEncoding];
}
size_t NSData_length(nsobj_t data) {
    return [(__bridge NSData *) data length];
}
const void *NSData_bytes(nsobj_t data) {
    return [(__bridge NSData *) data bytes];
}

#endif
