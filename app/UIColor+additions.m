//
//  UIColor+isLight.m
//  iSH
//
//  Created by Corban Amouzou on 2021-06-09.
//

#import "UIColor+additions.h"

@implementation UIColor (additions)

+ (UIColor *) colorWithHexString:(NSString *)hex {
    const char *colorString = [hex cStringUsingEncoding:NSASCIIStringEncoding];
    UInt32 newHex = (UInt32) strtol(colorString+1, nil, 16);
    return [UIColor colorWithHex:newHex];
}

+ (UIColor *) colorWithHex:(UInt32)color {
    unsigned char r, g, b;
    b = color & 0xFF;
    g = (color >> 8) & 0xFF;
    r = (color >> 16) & 0xFF;
    return [UIColor colorWithRed:(CGFloat)r/255.0f green:(CGFloat)g/255.0f blue:(CGFloat)b/255.0f alpha:1.0];
}

+ (NSString *) hexWithColor:(UIColor *)color {
    CGFloat r, g, b;
    [color getRed:&r green:&g blue:&b alpha:nil];
    
    return [NSString stringWithFormat:@"#%02lX%02lX%02lX",
            lroundf(r * 255),
            lroundf(g * 255),
            lroundf(b * 255)];
}

- (BOOL) isLight {
    const CGFloat *components = CGColorGetComponents(self.CGColor);
    double brightness = ((components[0] * 299) + (components[1] * 587) + (components[2] * 114)) / 1000;
    return !(brightness < 0.5);
}

@end
