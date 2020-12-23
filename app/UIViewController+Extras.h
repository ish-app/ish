//
//  UIViewController+Extras.h
//  iSH
//
//  Created by Theodore Dubois on 9/23/18.
//

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface UIViewController (Extras)

- (void)presentError:(NSError *)error title:(NSString *)title;

@end

NS_ASSUME_NONNULL_END
