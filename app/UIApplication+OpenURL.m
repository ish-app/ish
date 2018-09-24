//
//  UIApplication+OpenURL.m
//  iSH
//
//  Created by Theodore Dubois on 9/23/18.
//

#import "UIApplication+OpenURL.h"

@implementation UIApplication (OpenURL)

+ (void)openURL:(NSString *)url {
    [[self sharedApplication] openURL:[NSURL URLWithString:url] options:@{} completionHandler:nil];
}

@end
