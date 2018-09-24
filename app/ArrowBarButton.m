//
//  ArrowBarButton.m
//  iSH
//
//  Created by Theodore Dubois on 9/23/18.
//

#import "ArrowBarButton.h"


@interface ArrowBarButton () <CALayerDelegate> {
    CALayer *arrowLayers[5];
}

@property CGPoint startPoint;
@property (nonatomic) ArrowDirection direction;
@property NSTimer *timer;

@property UIColor *defaultColor;

@end

@implementation ArrowBarButton

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        [self setupLayers];
    }
    return self;
}
- (instancetype)initWithCoder:(NSCoder *)aDecoder {
    if (self = [super initWithCoder:aDecoder]) {
        [self setupLayers];
    }
    return self;
}

static CGPoint anchors[] = {
    {},
    {.5, .95}, // ArrowUp
    {.5, .05}, // ArrowDown
    {1.05, .5}, // ArrowLeft
    {-.05, .5}, // ArrowRight
};

- (void)setupLayers {
    self.layer.delegate = self;
    [self addTextLayer:@"↑" direction:ArrowUp];
    [self addTextLayer:@"↓" direction:ArrowDown];
    [self addTextLayer:@"←" direction:ArrowLeft];
    [self addTextLayer:@"→" direction:ArrowRight];
    [self layoutSublayersOfLayer:self.layer];
    
    self.layer.cornerRadius = 5;
    self.layer.shadowOffset = CGSizeMake(0, 1);
    self.layer.shadowOpacity = 0.4;
    self.layer.shadowRadius = 0;
    self.defaultColor = self.backgroundColor;
}

- (void)addTextLayer:(NSString *)text direction:(ArrowDirection)direction {
    CATextLayer *layer = [CATextLayer new];
    layer.contentsScale = UIScreen.mainScreen.scale;
    layer.string = text;
    layer.fontSize = 15;
    UIFont *font = [UIFont systemFontOfSize:layer.fontSize];
    layer.font = (__bridge CFTypeRef _Nullable) font;
    CGSize textSize = [[NSAttributedString alloc] initWithString:text attributes:@{NSFontAttributeName: font}].size;
    layer.bounds = CGRectMake(0, 0, textSize.width, textSize.height);
    layer.foregroundColor = UIColor.blackColor.CGColor;
    
    layer.alignmentMode = kCAAlignmentCenter;
    layer.anchorPoint = anchors[direction];
    [self.layer addSublayer:layer];
    self->arrowLayers[direction] = layer;
}

- (void)layoutSublayersOfLayer:(CALayer *)superlayer {
    NSParameterAssert(superlayer == self.layer);
    for (CALayer *layer in superlayer.sublayers) {
        layer.position = CGPointMake(self.bounds.size.width / 2, self.bounds.size.height / 2);
    }
}

- (BOOL)beginTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    [super beginTrackingWithTouch:touch withEvent:event];
    self.startPoint = [touch locationInView:self];
    self.selected = YES;
    return YES;
}

- (BOOL)continueTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    [super continueTrackingWithTouch:touch withEvent:event];
    CGPoint currentPoint = [touch locationInView:self];
    CGPoint diff = CGPointMake(currentPoint.x - self.startPoint.x, currentPoint.y - self.startPoint.y);
    if (hypot(diff.x, diff.y) < 20) {
        self.direction = ArrowNone;
    } else if (fabs(diff.x) > fabs(diff.y)) {
        // more to the side
        if (diff.x > 0)
            self.direction = ArrowRight;
        else
            self.direction = ArrowLeft;
    } else {
        // more up and down
        if (diff.y > 0)
            self.direction = ArrowDown;
        else
            self.direction = ArrowUp;
    }
    return YES;
}

- (void)endTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    [super endTrackingWithTouch:touch withEvent:event];
    self.selected = NO;
    self.direction = ArrowNone;
}
- (void)cancelTrackingWithEvent:(UIEvent *)event {
    [super cancelTrackingWithEvent:event];
    self.selected = NO;
    self.direction = ArrowNone;
}

- (void)chooseBackground {
    if (self.selected || self.highlighted) {
        self.backgroundColor = self.highlightedBackgroundColor;
    } else {
        self.backgroundColor = self.defaultColor;
    }
}

- (void)animateLayerUpdates {
    [UIView animateWithDuration:0.25 animations:^{
        for (int d = ArrowUp; d <= ArrowRight; d++) {
            CALayer *layer = self->arrowLayers[d];
            if (self.direction == ArrowNone || self.direction != d) {
                layer.opacity = self.selected ? 0.25 : 1;
            } else {
                layer.opacity = 1;
            }
        }
    }];
}

- (void)setDirection:(ArrowDirection)direction {
    ArrowDirection oldDirection = _direction;
    _direction = direction;
    if (direction != oldDirection) {
        [self animateLayerUpdates];
        [self.timer invalidate];
        if (direction != ArrowNone) {
            [self sendActionsForControlEvents:UIControlEventValueChanged];
            self.timer = [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:NO block:^(NSTimer *timer) {
                [self sendActionsForControlEvents:UIControlEventValueChanged];
                self.timer = [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *timer) {
                    [self sendActionsForControlEvents:UIControlEventValueChanged];
                }];
            }];
        }
    }
}

- (void)setSelected:(BOOL)selected {
    [super setSelected:selected];
    if (self.selected) {
        self.backgroundColor = self.highlightedBackgroundColor;
    } else {
        self.backgroundColor = self.defaultColor;
    }
    [self animateLayerUpdates];
}
@end
