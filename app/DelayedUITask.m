//
//  DelayedUITask.m
//  iSH
//
//  Created by Theodore Dubois on 11/8/17.
//

#import "DelayedUITask.h"

@interface DelayedUITask ()

@property id target;
@property SEL action;
@property NSTimer *timer;

@end

@implementation DelayedUITask

- (instancetype)initWithTarget:(id)target action:(SEL)action {
    if (self = [super init]) {
        self.target = target;
        self.action = action;
    }
    return self;
}

- (void)schedule {
    if (!self.timer.valid) {
        self.timer = [NSTimer timerWithTimeInterval:1.0/60 target:self.target selector:self.action userInfo:nil repeats:NO];
        [NSRunLoop.mainRunLoop addTimer:self.timer forMode:NSDefaultRunLoopMode];
    }
}

@end
