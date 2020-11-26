//
//  PassthroughView.m
//  iSH
//
//  Created by Theodore Dubois on 11/24/20.
//

#import "PassthroughView.h"

@implementation PassthroughView

- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
    for (UIView *subview in self.subviews) {
        if (subview.userInteractionEnabled && [subview pointInside:[self convertPoint:point toView:subview] withEvent:event])
            return YES;
    }
    return NO;
}

@end
