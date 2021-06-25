//
//  EditSchemeViewController.h
//  iSH
//
//  Created by Corban Amouzou on 2021-06-11.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import "UserPreferences.h"
NS_ASSUME_NONNULL_BEGIN
@protocol EditSchemeViewControllerDelegate
@required
- (void) schemeChanged;
@end

@interface EditSchemeViewController : UITableViewController <UIColorPickerViewControllerDelegate> {
    UIColor *oldBackgroundColor;
    UIColor *oldForegroundColor;
    NSString *editingPropertyName;
}
@property NSString *schemeName;
@property Scheme *currentScheme;
@property UIViewController <EditSchemeViewControllerDelegate>* delegate;
@end
NS_ASSUME_NONNULL_END
