//
//  CALayer+ShadowEnhancement.m
//  iSH
//
//  Created by Corban Amouzou on 2021-06-09.
//

#import "CALayer+ShadowEnhancement.h"

@implementation CALayer (AboutThemeExtention)

- (void) setAdvancedShadowWithColor:(UIColor *)color
                              alpha:(float)alpha
                                  x:(CGFloat)x
                                  y:(CGFloat)y
                               blur:(CGFloat)blur
                             spread:(CGFloat)spread {
    self.masksToBounds = false;
    self.shadowColor = color.CGColor;
    self.shadowOpacity = alpha;
    self.shadowOffset = CGSizeMake(x, y);
    self.shadowRadius = blur / 2.0;
    if (spread == 0) {
        self.shadowPath = nil;
    } else {
        CGFloat dx = -spread;
        CGRect rect = CGRectInset(self.bounds, dx, dx);
        self.shadowPath = [UIBezierPath bezierPathWithRect:rect].CGPath;
    }
}

@end
