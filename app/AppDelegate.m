//
//  AppDelegate.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#include <resolv.h>
#include <arpa/inet.h>
#include <netdb.h>
#import "AppDelegate.h"
#import "TerminalViewController.h"
#import "UserPreferences.h"
#include "kernel/init.h"
#include "kernel/calls.h"

@interface AppDelegate ()

@property BOOL exiting;

@end

static void ios_handle_exit(int code) {
    [[NSNotificationCenter defaultCenter] postNotificationName:ISHExitedNotification object:nil];
}

@implementation AppDelegate

- (int)startThings {
    NSFileManager *manager = [NSFileManager defaultManager];
    NSURL *container = [manager containerURLForSecurityApplicationGroupIdentifier:@"group.app.ish.iSH"];
    NSURL *alpineRoot = [container URLByAppendingPathComponent:@"roots/alpine"];
    [manager createDirectoryAtURL:[container URLByAppendingPathComponent:@"roots"]
      withIntermediateDirectories:YES
                       attributes:@{}
                            error:nil];
    
#if 0
    // copy the files to the app container so I can more easily get them out
    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask][0];
    [NSFileManager.defaultManager removeItemAtURL:[documents URLByAppendingPathComponent:@"roots copy"] error:nil];
    [NSFileManager.defaultManager copyItemAtURL:[container URLByAppendingPathComponent:@"roots"]
                                          toURL:[documents URLByAppendingPathComponent:@"roots copy"]
                                          error:nil];
#endif
    
    if (![manager fileExistsAtPath:alpineRoot.path]) {
        NSURL *alpineMaster = [NSBundle.mainBundle URLForResource:@"alpine" withExtension:nil];
        NSError *error = nil;
        [manager copyItemAtURL:alpineMaster toURL:alpineRoot error:&error];
        if (error != nil) {
            NSLog(@"%@", error);
            exit(1);
        }
    }
    alpineRoot = [alpineRoot URLByAppendingPathComponent:@"data"];
    int err = mount_root(&fakefs, alpineRoot.fileSystemRepresentation);
    if (err < 0)
        return err;
    
    create_first_process();
    NSArray<NSString *> *command = UserPreferences.shared.launchCommand;
    char *argv[command.count + 1];
    for (NSUInteger i = 0; i < command.count; i++) {
        argv[i] = (char *) command[i].UTF8String;
    }
    argv[command.count] = NULL;
    char *envp[] = {"TERM=xterm-256color", NULL};
    err = sys_execve(argv[0], argv, envp);
    if (err < 0)
        return err;
    err = create_stdio(&ios_tty_driver);
    if (err < 0)
        return err;
    exit_hook = ios_handle_exit;
    
    // configure dns
    struct __res_state res;
    if (EXIT_SUCCESS != res_ninit(&res)) {
        exit(2);
    }
    NSMutableString *resolvConf = [NSMutableString new];
    for (int i = 0; res.dnsrch[i] != NULL; i++) {
        [resolvConf appendFormat:@"search %s\n", res.dnsrch[i]];
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
    struct fd *fd = generic_open("/etc/resolv.conf", O_WRONLY_ | O_CREAT_ | O_TRUNC_, 0666);
    if (!IS_ERR(fd)) {
        fd->ops->write(fd, resolvConf.UTF8String, [resolvConf lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
        fd_close(fd);
    }
    
    // create some device nodes
    // this will do nothing if they already exist
    generic_mknod("/dev/tty", S_IFCHR|0666, dev_make(5, 0));
    generic_mknod("/dev/ptmx", S_IFCHR|0666, dev_make(5, 2));
    generic_mknod("/dev/random", S_IFCHR|0666, dev_make(1, 8));
    generic_mknod("/dev/urandom", S_IFCHR|0666, dev_make(1, 9));

    do_mount(&procfs, "proc", "/proc");
    do_mount(&devptsfs, "devpts", "/dev/pts");

    task_start(current);
    return 0;
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // get the network permissions popup to appear on chinese devices
    [[NSURLSession.sharedSession dataTaskWithURL:[NSURL URLWithString:@"http://captive.apple.com"]] resume];
    
    [UserPreferences.shared addObserver:self forKeyPath:@"shouldDisableDimming" options:NSKeyValueObservingOptionInitial context:nil];
    int err = [self startThings];
    if (err < 0) {
        NSString *message = [NSString stringWithFormat:@"could not initialize"];
        NSString *subtitle = [NSString stringWithFormat:@"error code %d", err];
        if (err == _EINVAL)
            subtitle = [subtitle stringByAppendingString:@"\n(try reinstalling the app, see release notes for details)"];
        [self showMessage:message subtitle:subtitle fatal:NO];
        NSLog(@"failed with code %d", err);
    }
    return YES;
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    UIApplication.sharedApplication.idleTimerDisabled = UserPreferences.shared.shouldDisableDimming;
}

- (void)showMessage:(NSString *)message subtitle:(NSString *)subtitle fatal:(BOOL)fatal {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:message message:subtitle preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"k"
                                                  style:UIAlertActionStyleDefault
                                                handler:nil]];
        [self.window.rootViewController presentViewController:alert animated:YES completion:nil];
    });
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

NSString *const ISHExitedNotification = @"ISHExitedNotification";
