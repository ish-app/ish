//
//  AccessoryButton.m
//  iSH
//
//  Created by Theodore Dubois on 9/22/18.
//

#import "BarButton.h"

@interface BarButton ()
@property UIColor *defaultColor;
@end

@implementation BarButton

- (void)awakeFromNib {
    [super awakeFromNib];
    self.layer.cornerRadius = 5;
    self.layer.shadowOffset = CGSizeMake(0, 1);
    self.layer.shadowOpacity = 0.4;
    self.layer.shadowRadius = 0;
    self.defaultColor = self.backgroundColor;
}

- (void)chooseBackground {
    if (self.selected || self.highlighted) {
        self.backgroundColor = self.highlightedBackgroundColor;
    } else {
        self.backgroundColor = self.defaultColor;
    }
}

- (void)setHighlighted:(BOOL)highlighted {
    [super setHighlighted:highlighted];
    [self chooseBackground];
}
- (void)setSelected:(BOOL)selected {
    [super setSelected:selected];
    [self chooseBackground];
}

@end
