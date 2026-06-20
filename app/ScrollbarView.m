//
//  ScrollbarView.m
//  iSH
//
//  Created by Theodore Dubois on 9/2/19.
//

#import "ScrollbarView.h"

@class ScrollbarViewDelegate;

@interface ScrollbarView ()

@property CGPoint contentViewOrigin;
@property ScrollbarViewDelegate *outerDelegate;

@end

@interface ScrollbarViewDelegate : NSObject <UIScrollViewDelegate>
@property (weak) id<UIScrollViewDelegate> innerDelegate;
@end
@implementation ScrollbarViewDelegate

- (void)scrollViewDidScroll:(ScrollbarView *)scrollView {
    CGRect frame = scrollView.contentView.frame;
    frame.origin.x = scrollView.contentOffset.x + scrollView.contentViewOrigin.x;
    frame.origin.y = scrollView.contentOffset.y + scrollView.contentViewOrigin.y;
    scrollView.contentView.frame = frame;
    [self.innerDelegate scrollViewDidScroll:scrollView];
}

- (id)forwardingTargetForSelector:(SEL)aSelector {
    if ([self.innerDelegate respondsToSelector:aSelector])
        return self.innerDelegate;
    return [super forwardingTargetForSelector:aSelector];
}

@end

@implementation ScrollbarView

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        self.outerDelegate = [ScrollbarViewDelegate new];
        super.delegate = self.outerDelegate;
    }
    return self;
}

- (void)setContentView:(UIView *)contentView {
    _contentView = contentView;
    self.contentViewOrigin = contentView.frame.origin;
}

- (id<UIScrollViewDelegate>)delegate {
    return self.outerDelegate.innerDelegate;
}

- (void)setDelegate:(id<UIScrollViewDelegate>)delegate {
    self.outerDelegate.innerDelegate = delegate;
}

@end
