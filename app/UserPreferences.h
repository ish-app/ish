//
//  UserPreferences.h
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, UserPreferenceTheme) {
    UserPreferenceThemeLight,
    UserPreferenceThemeDark
};

NS_ASSUME_NONNULL_BEGIN

extern UIColor *ThemeBackgroundColor(UserPreferenceTheme theme);
extern UIColor *ThemeForegroundColor(UserPreferenceTheme theme);

@interface UserPreferences : NSObject

@property (nonatomic) BOOL mapCapsLockAsControl;
@property (nonatomic) UserPreferenceTheme theme;
@property (nonatomic, copy) NSNumber *fontSize;

+ (instancetype)shared;
- (NSString *)JSONDictionary;

@end

NS_ASSUME_NONNULL_END
