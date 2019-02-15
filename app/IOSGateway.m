//
//  IOSGateway.m
//  iSH
//
//  Created by Miguel Vanhove on 15/02/2019.
//

#import "IOSGateway.h"
#import "IACManager.h"
#import "UIApplication+OpenURL.h"

static IOSGateway *iOSGateway = nil;

@interface IOSGateway ()
{
}

@property (copy, nonatomic) NSData *iacResult;

@end

@implementation IOSGateway
{
}

+ (IOSGateway *_Nullable)sharedSession
{
    if (iOSGateway == nil) {
        iOSGateway = [[IOSGateway alloc] init];
    }

    return iOSGateway;
}

- (id)init
{
    self = [super init];
    if (self != nil) {
        self.iacResult = [[NSData alloc] init];
    }
    return self;
}

- (void)setup
{
#ifndef TARGET_IS_EXTENSION

    [IACManager sharedManager].callbackURLScheme = @"x-ish";

    [[IACManager sharedManager] handleAction:@"iac"
                                   withBlock: ^(NSDictionary *inputParameters, IACSuccessBlock success, IACFailureBlock failure) {
        if (success) {
            NSError *__autoreleasing jserr = nil;

            self.iacResult = [NSJSONSerialization dataWithJSONObject:inputParameters options:0 error:&jserr];

            success(@{ @"names": @"json" }, NO);
        }
    }];
#endif
}

- (BOOL)handleOpenURL:(NSURL *)url
{
#ifndef TARGET_IS_EXTENSION
    return [[IACManager sharedManager] handleOpenURL:url];
#else
    return false;
#endif
}

extern size_t iac_write(const void *buf, size_t bufsize)
{
#ifndef TARGET_IS_EXTENSION

    NSData *data = [NSData dataWithBytes:buf length:bufsize];
    NSString *command = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];

    dispatch_sync(dispatch_get_main_queue(), ^{
        IOSGateway *ic = [IOSGateway sharedSession];

        NSString *cmdString = [command stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];

        ic.iacResult = [[NSData alloc] init];      // Reset result
        
        [UIApplication openURL:cmdString];
    });

#endif

    return bufsize;
}

extern size_t iac_read(void *buf, size_t bufsize)
{
#ifndef TARGET_IS_EXTENSION

    IOSGateway *ic = [IOSGateway sharedSession];

    if (bufsize < [ic.iacResult length]) {
        memcpy(buf, [ic.iacResult bytes], bufsize);

        ic.iacResult = [ic.iacResult subdataWithRange:NSMakeRange(bufsize, [ic.iacResult length] - bufsize)];

        return bufsize;
    }

    NSUInteger length = [ic.iacResult length];

    memcpy(buf, [ic.iacResult bytes], length);

    ic.iacResult = [[NSData alloc] init];

    return length;

#endif

    return 0;
}

@end
