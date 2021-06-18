//
//  UIColor+isLight.h
//  iSH
//
//  Created by Corban Amouzou on 2021-06-09.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface UIColor (additions)
+ (UIColor *)colorWithHexString:(NSString *)hex;
+ (UIColor *)colorWithHex:(UInt32)color;
- (BOOL) isLight;
@end

NS_ASSUME_NONNULL_END
