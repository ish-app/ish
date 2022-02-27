//
//  ThemeViewController.h
//  libiSHApp
//
//  Created by Saagar Jha on 7/16/22.
//

#import <UIKit/UIKit.h>
#import "Theme.h"

NS_ASSUME_NONNULL_BEGIN

@interface ThemeViewController : UITableViewController
@property Theme *theme;
@property BOOL isEditable;
@end

NS_ASSUME_NONNULL_END
