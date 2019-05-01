//
//  IACClient.m
//  IACSample
//
//  Created by Antonio Cabezuelo Vivo on 09/02/13.
//  Copyright (c) 2013 Antonio Cabezuelo Vivo. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "IACClient.h"
#import "IACRequest.h"
#import "IACManager.h"

#if !__has_feature(objc_arc)
#error InterAppComutication must be built with ARC.
// You can turn on ARC for only InterAppComutication files by adding -fobjc-arc to the build phase for each of its files.
#endif


@implementation IACClient

+ (instancetype)client {
    return [[self alloc] init];
}

+ (instancetype)clientWithURLScheme:(NSString*)scheme {
    return [[self alloc] initWithURLScheme:scheme];
}

- (instancetype)initWithURLScheme:(NSString*)scheme {
    self = [super init];
    if (self) {
        self.URLScheme = scheme;
    }
    return self;
}

- (NSInteger)NSErrorCodeForXCUErrorCode:(NSString*)code {
    return [code integerValue];
}

- (BOOL)isAppInstalled {
    return [[UIApplication sharedApplication] canOpenURL:[NSURL URLWithString:[NSString stringWithFormat:@"%@://Test", self.URLScheme]]];
}

- (void)performAction:(NSString*)action {
    [self performAction:action parameters:nil];
}

- (void)performAction:(NSString*)action parameters:(NSDictionary*)params {
    [self performAction:action parameters:params onSuccess:nil onFailure:nil];
}


- (void)performAction:(NSString*)action parameters:(NSDictionary*)params onSuccess:(void(^)(NSDictionary*result))success onFailure:(void(^)(NSError*))failure {
    
    IACRequest *request = [[IACRequest alloc] init];
    request.client = self;
    request.action = action;
    request.parameters = params;
    request.successCalback = success;
    request.errorCalback = failure;
    
    if (self.manager) {
        [self.manager sendIACRequest:request];
    } else {
        [[IACManager sharedManager] sendIACRequest:request];
    }
}

@end
