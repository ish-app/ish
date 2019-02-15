//
//  AppDelegate.h
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import <UIKit/UIKit.h>

#define kGroupName @"group.app.ish.iSH"

@interface AppDelegate : UIResponder <UIApplicationDelegate>

@property (strong, nonatomic) UIWindow *window;
- (void)exitApp;

@end

extern NSString *const ISHExitedNotification;
