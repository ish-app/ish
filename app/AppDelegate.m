//
//  AppDelegate.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#include <resolv.h>
#include <arpa/inet.h>
#include <netdb.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import "AboutViewController.h"
#import "AppDelegate.h"
#import "AppGroup.h"
#import "APKFilesystem.h"
#import "iOSFS.h"
#import "SceneDelegate.h"
#import "PasteboardDevice.h"
#import "LocationDevice.h"
#import "NSObject+SaneKVO.h"
#import "Roots.h"
#import "TerminalViewController.h"
#import "UserPreferences.h"
#import "UIApplication+OpenURL.h"
#include "kernel/init.h"
#include "kernel/calls.h"
#include "fs/dyndev.h"
#include "fs/devices.h"
#include "fs/path.h"

@interface AppDelegate ()

@property BOOL exiting;
@property NSString *unameVersion;
@property NSString *unameHostname;
@property SCNetworkReachabilityRef reachability;

@end

static void ios_handle_exit(struct task *task, int code) {
    // we are interested in init and in children of init
    // this is called with pids_lock as an implementation side effect, please do not cite as an example of good API design
    if (task->parent != NULL && task->parent->parent != NULL)
        return;
    // pid should be saved now since task would be freed
    pid_t pid = task->pid;
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:ProcessExitedNotification
                                                            object:nil
                                                          userInfo:@{@"pid": @(pid),
                                                                     @"code": @(code)}];
    });
}

// Put the abort message in the thread name so it gets included in the crash dump
static void ios_handle_die(const char *msg) {
    char name[17];
    pthread_getname_np(pthread_self(), name, sizeof(name));
    NSString *newName = [NSString stringWithFormat:@"%s died: %s", name, msg];
    pthread_setname_np(newName.UTF8String);
}

static int bootError;
static int fs_ish_version;
static NSString *const kSkipStartupMessage = @"Skip Startup Message";

@implementation AppDelegate

- (int)boot {
    NSURL *root = [[Roots.instance rootUrl:Roots.instance.defaultRoot] URLByAppendingPathComponent:@"data"];
    int err = mount_root(&fakefs, root.fileSystemRepresentation);
    if (err < 0)
        return err;

    fs_register(&iosfs);
    fs_register(&iosfs_unsafe);
    fs_register(&apkfs);

    // need to do this first so that we can have a valid current for the generic_mknod calls
    err = become_first_process();
    if (err < 0)
        return err;

    // /ish/version is the last ish version that opened this root. Used to migrate the filesystem.
    struct fd *ish_version_fd = generic_open("/ish/version", O_RDONLY_, 0);
    if (!IS_ERR(ish_version_fd)) {
        char buf[100];
        ssize_t n = ish_version_fd->ops->read(ish_version_fd, buf, sizeof(buf));
        if (n < 0)
            return (int) n;
        NSString *version = [[NSString alloc] initWithBytesNoCopy:buf length:n encoding:NSUTF8StringEncoding freeWhenDone:NO];
        version = [version stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        fs_ish_version = version.intValue;
        fd_close(ish_version_fd);

        // I forgot to add the community repo
        if (fs_ish_version < 88) {
            NSData *repositoriesData = [NSData dataWithContentsOfURL:[root URLByAppendingPathComponent:@"etc/apk/repositories"]];
            NSString *repositories = [[NSString alloc] initWithData:repositoriesData encoding:NSUTF8StringEncoding];
            NSString *communityRepo = @"file:///ish/apk/community";
            if (![[repositories componentsSeparatedByString:@"\n"] containsObject:communityRepo]) {
                NSString *addend = [communityRepo stringByAppendingString:@"\n"];
                struct fd *repositories_fd = generic_open("/etc/apk/repositories", O_WRONLY_|O_APPEND_, 0);
                if (!IS_ERR(repositories_fd)) {
                    repositories_fd->ops->write(repositories_fd, addend.UTF8String, [addend lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
                    fd_close(repositories_fd);
                }
            }
        }

        NSString *currentVersion = NSBundle.mainBundle.infoDictionary[(__bridge NSString *) kCFBundleVersionKey];
        if (currentVersion.intValue > fs_ish_version) {
            fs_ish_version = currentVersion.intValue;
            ish_version_fd = generic_open("/ish/version", O_WRONLY_|O_TRUNC_, 0644);
            if (!IS_ERR(ish_version_fd)) {
                NSString *file = [NSString stringWithFormat:@"%@\n", currentVersion];
                ish_version_fd->ops->write(ish_version_fd, file.UTF8String, [file lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
                fd_close(ish_version_fd);
            }
        }

        if ([NSBundle.mainBundle URLForResource:@"OnDemandResources" withExtension:@"plist"] != nil) {
            generic_mkdirat(AT_PWD, "/ish/apk", 0755);
            do_mount(&apkfs, "apk", "/ish/apk", "", 0);
        }
    }

    // create some device nodes
    // this will do nothing if they already exist
    generic_mknodat(AT_PWD, "/dev/tty1", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 1));
    generic_mknodat(AT_PWD, "/dev/tty2", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 2));
    generic_mknodat(AT_PWD, "/dev/tty3", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 3));
    generic_mknodat(AT_PWD, "/dev/tty4", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 4));
    generic_mknodat(AT_PWD, "/dev/tty5", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 5));
    generic_mknodat(AT_PWD, "/dev/tty6", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 6));
    generic_mknodat(AT_PWD, "/dev/tty7", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 7));

    generic_mknodat(AT_PWD, "/dev/tty", S_IFCHR|0666, dev_make(TTY_ALTERNATE_MAJOR, DEV_TTY_MINOR));
    generic_mknodat(AT_PWD, "/dev/console", S_IFCHR|0666, dev_make(TTY_ALTERNATE_MAJOR, DEV_CONSOLE_MINOR));
    generic_mknodat(AT_PWD, "/dev/ptmx", S_IFCHR|0666, dev_make(TTY_ALTERNATE_MAJOR, DEV_PTMX_MINOR));

    generic_mknodat(AT_PWD, "/dev/null", S_IFCHR|0666, dev_make(MEM_MAJOR, DEV_NULL_MINOR));
    generic_mknodat(AT_PWD, "/dev/zero", S_IFCHR|0666, dev_make(MEM_MAJOR, DEV_ZERO_MINOR));
    generic_mknodat(AT_PWD, "/dev/full", S_IFCHR|0666, dev_make(MEM_MAJOR, DEV_FULL_MINOR));
    generic_mknodat(AT_PWD, "/dev/random", S_IFCHR|0666, dev_make(MEM_MAJOR, DEV_RANDOM_MINOR));
    generic_mknodat(AT_PWD, "/dev/urandom", S_IFCHR|0666, dev_make(MEM_MAJOR, DEV_URANDOM_MINOR));
    
    generic_mkdirat(AT_PWD, "/dev/pts", 0755);
    
    // Permissions on / have been broken for a while, let's fix them
    generic_setattrat(AT_PWD, "/", (struct attr) {.type = attr_mode, .mode = 0755}, false);
    
    // Register clipboard device driver and create device node for it
    err = dyn_dev_register(&clipboard_dev, DEV_CHAR, DYN_DEV_MAJOR, DEV_CLIPBOARD_MINOR);
    if (err != 0) {
        return err;
    }
    generic_mknodat(AT_PWD, "/dev/clipboard", S_IFCHR|0666, dev_make(DYN_DEV_MAJOR, DEV_CLIPBOARD_MINOR));
    
    err = dyn_dev_register(&location_dev, DEV_CHAR, DYN_DEV_MAJOR, DEV_LOCATION_MINOR);
    if (err != 0)
        return err;
    generic_mknodat(AT_PWD, "/dev/location", S_IFCHR|0666, dev_make(DYN_DEV_MAJOR, DEV_LOCATION_MINOR));

    do_mount(&procfs, "proc", "/proc", "", 0);
    do_mount(&devptsfs, "devpts", "/dev/pts", "", 0);

    iosfs_init(); // let it mount any filesystems from user defaults

    [self configureDns];
    
    exit_hook = ios_handle_exit;
    die_handler = ios_handle_die;
#if !TARGET_OS_SIMULATOR
    NSString *sockTmp = [NSTemporaryDirectory() stringByAppendingString:@"ishsock"];
    sock_tmp_prefix = strdup(sockTmp.UTF8String);
#endif
    
    tty_drivers[TTY_CONSOLE_MAJOR] = &ios_console_driver;
    set_console_device(TTY_CONSOLE_MAJOR, 1);
    err = create_stdio("/dev/console", TTY_CONSOLE_MAJOR, 1);
    if (err < 0)
        return err;
    
    NSArray<NSString *> *command;
    command = UserPreferences.shared.bootCommand;
    NSLog(@"%@", command);
    char argv[4096];
    [Terminal convertCommand:command toArgs:argv limitSize:sizeof(argv)];
    const char *envp = "TERM=xterm-256color\0";
    err = do_execve(command[0].UTF8String, command.count, argv, envp);
    if (err < 0)
        return err;
    task_start(current);
    
    return 0;
}

- (void)configureDns {
    struct __res_state res;
    if (EXIT_SUCCESS != res_ninit(&res)) {
        exit(2);
    }
    NSMutableString *resolvConf = [NSMutableString new];
    if (res.dnsrch[0] != NULL) {
        [resolvConf appendString:@"search"];
        for (int i = 0; res.dnsrch[i] != NULL; i++) {
            [resolvConf appendFormat:@" %s", res.dnsrch[i]];
        }
        [resolvConf appendString:@"\n"];
    }
    union res_sockaddr_union servers[NI_MAXSERV];
    int serversFound = res_getservers(&res, servers, NI_MAXSERV);
    char address[NI_MAXHOST];
    for (int i = 0; i < serversFound; i ++) {
        union res_sockaddr_union s = servers[i];
        if (s.sin.sin_len == 0)
            continue;
        getnameinfo((struct sockaddr *) &s.sin, s.sin.sin_len,
                    address, sizeof(address),
                    NULL, 0, NI_NUMERICHOST);
        [resolvConf appendFormat:@"nameserver %s\n", address];
    }
    
    current = pid_get_task(1);
    struct fd *fd = generic_open("/etc/resolv.conf", O_WRONLY_ | O_CREAT_ | O_TRUNC_, 0666);
    if (!IS_ERR(fd)) {
        fd->ops->write(fd, resolvConf.UTF8String, [resolvConf lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
        fd_close(fd);
    }
}

+ (int)bootError {
    return bootError;
}

+ (void)maybePresentStartupMessageOnViewController:(UIViewController *)vc {
    if ([NSUserDefaults.standardUserDefaults integerForKey:kSkipStartupMessage] >= 1)
        return;
    if (fs_ish_version == 0) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Install iSH’s built-in APK?"
                                                                       message:@"iSH now includes the APK package manager, but it must be manually activated."
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Show me how"
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction * _Nonnull action) {
            [UIApplication openURL:@"https://go.ish.app/get-apk"];
        }]];
        [alert addAction:[UIAlertAction actionWithTitle:@"Don't show again"
                                                  style:UIAlertActionStyleDefault
                                                handler:nil]];
        [vc presentViewController:alert animated:YES completion:nil];
    }
    [NSUserDefaults.standardUserDefaults setInteger:1 forKey:kSkipStartupMessage];
}

- (BOOL)application:(UIApplication *)application willFinishLaunchingWithOptions:(NSDictionary<UIApplicationLaunchOptionsKey,id> *)launchOptions {
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    if ([defaults boolForKey:@"hail mary"]) {
        [defaults removeObjectForKey:kPreferenceBootCommandKey];
        [defaults removeObjectForKey:kPreferenceLaunchCommandKey];
        [defaults setBool:NO forKey:@"hail mary"];
    }
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"recovery"])
        return YES;

    bootError = [self boot];
    return YES;
}

void NetworkReachabilityCallback(SCNetworkReachabilityRef target, SCNetworkReachabilityFlags flags, void *info) {
    AppDelegate *self = (__bridge AppDelegate *) info;
    [self configureDns];
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // get the network permissions popup to appear on chinese devices
    [[NSURLSession.sharedSession dataTaskWithURL:[NSURL URLWithString:@"http://captive.apple.com"]] resume];

    if ([NSUserDefaults.standardUserDefaults boolForKey:@"FASTLANE_SNAPSHOT"])
        [UIView setAnimationsEnabled:NO];
    
    self.unameVersion = [NSString stringWithFormat:@"iSH %@ (%@)",
                         [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"],
                         [NSBundle.mainBundle objectForInfoDictionaryKey:(NSString *) kCFBundleVersionKey]];
    extern const char *uname_version;
    uname_version = self.unameVersion.UTF8String;
    // this defaults key is set when taking app store screenshots
    self.unameHostname = [NSUserDefaults.standardUserDefaults stringForKey:@"hostnameOverride"];
    extern const char *uname_hostname_override;
    uname_hostname_override = self.unameHostname.UTF8String;
    
    [UserPreferences.shared observe:@[@"shouldDisableDimming"] options:NSKeyValueObservingOptionInitial
                              owner:self usingBlock:^(typeof(self) self) {
        UIApplication.sharedApplication.idleTimerDisabled = UserPreferences.shared.shouldDisableDimming;
    }];
    
    struct sockaddr_in6 address = {
        .sin6_len = sizeof(address),
        .sin6_family = AF_INET6,
    };
    self.reachability = SCNetworkReachabilityCreateWithAddress(kCFAllocatorDefault, (struct sockaddr *) &address);
    SCNetworkReachabilityContext context = {
        .info = (__bridge void *) self,
    };
    SCNetworkReachabilitySetCallback(self.reachability, NetworkReachabilityCallback, &context);
    SCNetworkReachabilityScheduleWithRunLoop(self.reachability, CFRunLoopGetMain(), kCFRunLoopCommonModes);

    if (self.window != nil) {
        // For iOS <13, where the app delegate owns the window instead of the scene
        if ([NSUserDefaults.standardUserDefaults boolForKey:@"recovery"]) {
            UINavigationController *vc = [[UIStoryboard storyboardWithName:@"About" bundle:nil] instantiateInitialViewController];
            AboutViewController *avc = (AboutViewController *) vc.topViewController;
            avc.recoveryMode = YES;
            self.window.rootViewController = vc;
            return YES;
        }
        TerminalViewController *vc = (TerminalViewController *) self.window.rootViewController;
        currentTerminalViewController = vc;
        [vc startNewSession];
    }
    return YES;
}

- (void)application:(UIApplication *)application didDiscardSceneSessions:(NSSet<UISceneSession *> *)sceneSessions API_AVAILABLE(ios(13.0)) {
    for (UISceneSession *sceneSession in sceneSessions) {
        NSString *terminalUUID = sceneSession.stateRestorationActivity.userInfo[@"TerminalUUID"];
        [[Terminal terminalWithUUID:[[NSUUID alloc] initWithUUIDString:terminalUUID]] destroy];
    }
}

- (void)dealloc {
    if (self.reachability != NULL) {
        SCNetworkReachabilityUnscheduleFromRunLoop(self.reachability, CFRunLoopGetMain(), kCFRunLoopCommonModes);
        CFRelease(self.reachability);
    }
}

- (void)exitApp {
    self.exiting = YES;
    id app = [UIApplication sharedApplication];
    [app suspend];
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
    if (self.exiting)
        exit(0);
}

@end

NSString *const ProcessExitedNotification = @"ProcessExitedNotification";
