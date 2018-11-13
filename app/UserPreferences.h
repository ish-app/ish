//
//  UserPreferences.h
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <Foundation/Foundation.h>

// Add new themes to the end of the enum (before count) for backwards compatibility
typedef NS_ENUM(NSInteger, UserPreferenceTheme) {
    UserPreferenceThemeLight,
    UserPreferenceThemeDark,
    UserPreferenceThemeCount
};

extern UIColor *ThemeBackgroundColor(UserPreferenceTheme theme);
extern UIColor *ThemeForegroundColor(UserPreferenceTheme theme);
extern UIStatusBarStyle ThemeStatusBar(UserPreferenceTheme theme);
extern UIKeyboardAppearance ThemeKeyboard(UserPreferenceTheme theme);
extern NSString *ThemeName(UserPreferenceTheme theme);

NS_ASSUME_NONNULL_BEGIN

@interface UserPreferences : NSObject

@property (nonatomic) BOOL mapCapsLockAsControl;
@property (nonatomic) UserPreferenceTheme theme;
@property (nonatomic, copy) NSNumber *fontSize;

+ (instancetype)shared;
- (NSString *)JSONDictionary;

@end

NS_ASSUME_NONNULL_END
