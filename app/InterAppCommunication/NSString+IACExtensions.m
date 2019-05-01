//
//  NSString+IACExtensions.m
//  IACSample
//
//  Created by Antonio Cabezuelo Vivo on 10/02/13.
//  Copyright (c) 2013 Antonio Cabezuelo Vivo. All rights reserved.
//

#import "NSString+IACExtensions.h"

#if !__has_feature(objc_arc)
#error InterAppComutication must be built with ARC.
// You can turn on ARC for only InterAppComutication files by adding -fobjc-arc to the build phase for each of its files.
#endif


@implementation NSString (IACExtensions)

+ (NSString*)stringWithUUID {
    CFUUIDRef uuid = CFUUIDCreate(kCFAllocatorDefault);
    NSString *uuidStr = (__bridge_transfer NSString *)CFUUIDCreateString(kCFAllocatorDefault, uuid);
    CFRelease(uuid);
    
    return uuidStr;
}


- (NSDictionary*)parseURLParams {
    NSMutableDictionary *result = [[NSMutableDictionary alloc] init];
    
    NSArray *pairs = [self componentsSeparatedByString:@"&"];
    
    [pairs enumerateObjectsUsingBlock:^(NSString *pair, NSUInteger idx, BOOL *stop) {
        NSArray *comps = [pair componentsSeparatedByString:@"="];
        if ([comps count] == 2) {
            [result setObject:[comps[1] stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding] forKey:comps[0]];
        }
    }];
    
    return result;
}

- (NSString*)stringByAppendingURLParams:(NSDictionary*)params {
    NSMutableString *result = [[NSMutableString alloc] init];
    
    [result appendString:self];
    
    if ([result rangeOfString:@"?"].location != NSNotFound) {
        if (![result hasSuffix:@"&"])
            [result appendString:@"&"];
    } else {
        [result appendString:@"?"];
    }
    
    [params enumerateKeysAndObjectsUsingBlock:^(id key, id obj, BOOL *stop) {
        NSString *escapedObj = obj;
        if ([obj isKindOfClass:[NSString class]]) {
            escapedObj = (NSString *)CFBridgingRelease(CFURLCreateStringByAddingPercentEscapes(
                                                                                               NULL,
                                                                                               (__bridge CFStringRef) obj,
                                                                                               NULL,
                                                                                               CFSTR("!*'();:@&=+$,/?%#[]"),
                                                                                               kCFStringEncodingUTF8));
        }
        [result appendFormat:@"%@=%@&", key, escapedObj];
    }];
    
    return result;
}

@end
