//
//  DelayedUITask.h
//  iSH
//
//  Created by Theodore Dubois on 11/8/17.
//

#import <Foundation/Foundation.h>

@interface DelayedUITask : NSObject

- (instancetype)initWithTarget:(id)target action:(SEL)action;
- (void)schedule;

@end
