//
//  AppDelegate.h
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import <UIKit/UIKit.h>
#import "TerminalViewControllerDelegate.h"

@interface AppDelegate : UIResponder <UIApplicationDelegate, TerminalViewControllerDelegate>

@property (strong, nonatomic) UIWindow *window;
- (void)exitApp;

@end

extern NSString *const ProcessExitedNotification;
