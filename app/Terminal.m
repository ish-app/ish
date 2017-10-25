//
//  Terminal.m
//  iSH
//
//  Created by Theodore Dubois on 10/18/17.
//

#import "Terminal.h"
#include "fs/tty.h"

@interface Terminal ()

@property NSString *content;

@end

@implementation Terminal

- (instancetype)init {
    if (self = [super init]) {
        self.content = @"";
    }
    return self;
}

+ (Terminal *)terminalWithType:(int)type number:(int)number {
    // there's only one terminal currently
    static Terminal *terminal = nil;
    if (terminal == nil)
        terminal = [Terminal new];
    return terminal;
}

- (size_t)write:(const void *)buf length:(size_t)len {
    NSString *str = [[NSString alloc] initWithBytes:buf length:len encoding:NSUTF8StringEncoding];
    NSLog(@"%@", str);
    self.content = [self.content stringByAppendingString:str];
    return len;
}

@end

static int ios_tty_open(struct tty *tty) {
    tty->data = (void *) CFBridgingRetain([Terminal terminalWithType:tty->type number:tty->num]);
    return 0;
}

static ssize_t ios_tty_write(struct tty *tty, const void *buf, size_t len) {
    Terminal *terminal = (__bridge Terminal *) tty->data;
    return [terminal write:buf length:len];
}

static void ios_tty_close(struct tty *tty) {
    CFBridgingRelease(tty->data);
}

struct tty_driver ios_tty_driver = {
    .open = ios_tty_open,
    .write = ios_tty_write,
    .close = ios_tty_close,
};
