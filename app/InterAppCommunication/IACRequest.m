//
//  IACRequest.m
//  IACSample
//
//  Created by Antonio Cabezuelo Vivo on 09/02/13.
//  Copyright (c) 2013 Antonio Cabezuelo Vivo. All rights reserved.
//

#import "IACRequest.h"
#import "NSString+IACExtensions.h"

#if !__has_feature(objc_arc)
#error InterAppComutication must be built with ARC.
// You can turn on ARC for only InterAppComutication files by adding -fobjc-arc to the build phase for each of its files.
#endif


@interface IACRequest ()
@property (copy, readwrite, nonatomic) NSString *requestID;
@end

@implementation IACRequest

- (instancetype)init {
    self = [super init];
    if (self) {
        self.requestID = [NSString stringWithUUID];
    }
    return self;
}


@end
