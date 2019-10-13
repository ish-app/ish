//
//  AccessoryButton.h
//  iSH
//
//  Created by Theodore Dubois on 9/22/18.
//

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface BarButton : UIButton

@property (nonatomic) UIKeyboardAppearance keyAppearance;
@property IBInspectable BOOL secondary;
@property IBInspectable BOOL toggleable;

@end

NS_ASSUME_NONNULL_END
