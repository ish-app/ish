//
//  AppDelegate.h
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import <UIKit/UIKit.h>

@interface AppDelegate : UIResponder <UIApplicationDelegate>

@property (strong, nonatomic) UIWindow *window;
- (void)exitApp;

+ (int)bootError;

+ (void)maybePresentStartupMessageOnViewController:(UIViewController *)vc;

@end

extern NSString *const ProcessExitedNotification;
