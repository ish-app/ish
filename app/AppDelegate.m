//
//  AppDelegate.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import "AppDelegate.h"
#import "TerminalViewController.h"
#include "kernel/init.h"
#include "kernel/process.h"

@interface AppDelegate ()

@end

@implementation AppDelegate

- (int)startThings {
    NSString *resourcePath = NSBundle.mainBundle.resourcePath;
    int err = mount_root(resourcePath.UTF8String);
    if (err < 0)
        return err;
    
    char *program = "hello-libc-static";
    char *argv[] = {program, NULL};
    char *envp[] = {NULL};
    err = create_init_process(program, argv, envp);
    if (err < 0)
        return err;
    err = create_stdio(ios_tty_driver);
    if (err < 0)
        return err;
    start_thread(current);
    return 0;
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    int err = [self startThings];
    if (err < 0) {
        NSLog(@"failed with code %d", err);
    }
    return YES;
}


- (void)applicationWillResignActive:(UIApplication *)application {
    // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
    // Use this method to pause ongoing tasks, disable timers, and invalidate graphics rendering callbacks. Games should use this method to pause the game.
}


- (void)applicationDidEnterBackground:(UIApplication *)application {
    // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later.
    // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
}


- (void)applicationWillEnterForeground:(UIApplication *)application {
    // Called as part of the transition from the background to the active state; here you can undo many of the changes made on entering the background.
}


- (void)applicationDidBecomeActive:(UIApplication *)application {
    // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
}


- (void)applicationWillTerminate:(UIApplication *)application {
    // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
}


@end
