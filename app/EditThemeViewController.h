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
@protocol EditThemeViewControllerDelegate
@required
- (void) themeChanged;
@end

@interface EditThemeViewController : UITableViewController <UIColorPickerViewControllerDelegate> {
    UIColor *oldBackgroundColor;
    UIColor *oldForegroundColor;
    NSString *editingPropertyName;
}
@property NSString *themeName;
@property Theme *currentTheme;
@property UIViewController <EditThemeViewControllerDelegate>* delegate;
@end
NS_ASSUME_NONNULL_END
