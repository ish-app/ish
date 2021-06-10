//
//  CALayer+ShadowEnhancement.h
//  iSH
//
//  Created by Corban Amouzou on 2021-06-09.
//
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
NS_ASSUME_NONNULL_BEGIN
@interface CALayer (ShadowEnhancement)
- (void) setAdvancedShadowWithColor:(UIColor *)color
                              alpha:(float)alpha
                                  x:(CGFloat)x
                                  y:(CGFloat)y
                               blur:(CGFloat)blur
                             spread:(CGFloat)spread;
@end
NS_ASSUME_NONNULL_END
