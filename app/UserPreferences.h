//
//  UserPreferences.h
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
typedef NS_ENUM(NSInteger, CapsLockMapping) {
    CapsLockMapNone = 0,
    CapsLockMapControl,
    CapsLockMapEscape,
};

typedef enum : NSUInteger {
    OptionMapNone = 0,
    OptionMapEsc,
} OptionMapping;

NS_ASSUME_NONNULL_BEGIN

@interface Theme : NSObject

- (instancetype)initWithProperties:(NSDictionary<NSString *, id> *)props;
- (NSDictionary<NSString *, id> *)properties;

+ (NSDictionary<NSString *, Theme *> *)presets;
+ (NSArray<NSString *> *)themeNames;
- (NSString *)presetName;

@property (nonatomic, readonly) UIColor *foregroundColor;
@property (nonatomic, readonly) UIColor *backgroundColor;
@property (nonatomic) NSString *name;
@property (readonly) UIKeyboardAppearance keyboardAppearance;
@property (readonly) UIStatusBarStyle statusBarStyle;

@end
extern NSString *const kThemeForegroundColor;
extern NSString *const kThemeBackgroundColor;
extern NSString *const kThemeName;
@interface UserPreferences : NSObject

@property CapsLockMapping capsLockMapping;
@property OptionMapping optionMapping;
@property BOOL backtickMapEscape;
@property BOOL hideExtraKeysWithExternalKeyboard;
@property BOOL overrideControlSpace;
@property (nonatomic) Theme *theme;
@property (nonatomic) BOOL shouldDisableDimming;
@property NSString *fontFamily;
@property NSNumber *fontSize;
@property NSArray<NSString *> *launchCommand;
@property NSArray<NSString *> *bootCommand;

+ (instancetype)shared;

- (BOOL)hasChangedLaunchCommand;
- (void)setThemeTo:(NSString *)name;
- (Theme *)themeFromName:(NSString *)name;
- (void)deleteTheme:(NSString *)themeName;
@end

extern NSString *const kPreferenceLaunchCommandKey;
extern NSString *const kPreferenceBootCommandKey;

NS_ASSUME_NONNULL_END
