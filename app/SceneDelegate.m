//
//  SceneDelegate.m
//  iSH
//
//  Created by Noah Peeters on 13.09.19.
//

#import "SceneDelegate.h"
#import "TerminalViewController.h"
#import "UserPreferences.h"
#import "AppDelegate.h"

#if ENABLE_MULTIWINDOW
@interface SceneDelegate ()

@property int sessionPid;
@property int ttyNumber;
@property (strong, nonatomic) UISceneSession *sceneSession;

@end

@implementation SceneDelegate

- (void)updateTTYNumberForSession:(UISceneSession *)session {
    if (UserPreferences.shared.bootEnabled) {
        if (session.stateRestorationActivity != nil) {
            NSDictionary<NSString *, NSNumber *> *userInfo = session.stateRestorationActivity.userInfo;

            NSNumber *userInfoTTYNumber = [userInfo objectForKey:@"ttynumber"];
            if (userInfoTTYNumber != nil) {
                if ([Terminal isTTYNumberFree:userInfoTTYNumber.intValue]) {
                    self.ttyNumber = userInfoTTYNumber.intValue;
                    return;
                }
            }
        }

        self.ttyNumber = [Terminal nextFreeTTYNumber];
    } else {
        self.ttyNumber = 1;
    }
}

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {

    UIWindowScene* windowScene = (UIWindowScene *)scene;
    if (windowScene == nil) return;
    self.sceneSession = session;

    [NSNotificationCenter.defaultCenter addObserver:self
    selector:@selector(processExited:)
        name:ProcessExitedNotification
      object:nil];

    self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
    TerminalViewController *terminalViewController = [[UIStoryboard storyboardWithName:@"Main" bundle:nil] instantiateViewControllerWithIdentifier:@"terminalVC"];

    terminalViewController.delegate = self;
    self.window.rootViewController = terminalViewController;

    [self updateTTYNumberForSession:session];

    [Terminal terminalWithTTYNumber:self.ttyNumber launchCommand:UserPreferences.shared.launchCommand completion:^(Terminal *terminal) {
        self.sessionPid = terminal.launchCommandPID;
        [terminalViewController switchTerminalToTTYNumber:self.ttyNumber];
    }];

    [self.window makeKeyAndVisible];
}

- (NSUserActivity *)stateRestorationActivityForScene:(UIScene *)scene {
    NSMutableDictionary<NSString *, NSNumber *> *userInfo = [NSMutableDictionary new];
    [userInfo setObject:[NSNumber numberWithLong:self.ttyNumber] forKey: @"ttynumber"];

    NSUserActivity *userActivity = [[NSUserActivity alloc] initWithActivityType:@"stateRestoration"];
    userActivity.title = @"Terminal";
    [userActivity addUserInfoEntriesFromDictionary:userInfo];

    return userActivity;
}

- (void)terminalViewController:(TerminalViewController *)terminalViewController requestedTTYWithNumber:(int)number {
    [terminalViewController switchTerminalToTTYNumber:number == 0 ? self.ttyNumber : number];
}

- (void)processExited:(NSNotification *)notif {
    int pid = [notif.userInfo[@"pid"] intValue];
    if (pid == self.sessionPid) {
        [self closeScene];
    }
}

- (void)closeScene {
    UIWindowSceneDestructionRequestOptions * options = [UIWindowSceneDestructionRequestOptions new];
    options.windowDismissalAnimation = UIWindowSceneDismissalAnimationStandard;

    [UIApplication.sharedApplication requestSceneSessionDestruction:self.sceneSession options:nil errorHandler:nil];
}

@end
#endif
