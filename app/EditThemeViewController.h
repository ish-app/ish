//
//  EditThemeViewController.h
//  iSH
//
//  Created by Corban Amouzou on 2021-06-11.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import "UserPreferences.h"
NS_ASSUME_NONNULL_BEGIN

@interface EditThemeViewController : UITableViewController {
    UIColor *oldBackgroundColor;
    UIColor *oldForegroundColor;
}
@property NSString *themeName;
@property Theme *currentTheme;
@end

NS_ASSUME_NONNULL_END
