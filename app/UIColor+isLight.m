//
//  UIColor+isLight.m
//  iSH
//
//  Created by Corban Amouzou on 2021-06-09.
//

#import "UIColor+isLight.h"

@implementation UIColor (isLight)

- (BOOL) isLight {
    const CGFloat *components = CGColorGetComponents(self.CGColor);
    double brightness = ((components[0] * 299) + (components[1] * 587) + (components[2] * 114)) / 1000;
    return !(brightness < 0.5);
}

@end
