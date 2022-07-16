//
//  UserPreferences.h
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, CapsLockMapping) {
    __CapsLockMapFirst = 0,
    CapsLockMapNone = 0,
    CapsLockMapControl,
    CapsLockMapEscape,
    __CapsLockMapLast,
};

typedef enum : NSUInteger {
    __OptionMapFirst = 0,
    OptionMapNone = 0,
    OptionMapEsc,
    __OptionMapLast,
} OptionMapping;

NS_ASSUME_NONNULL_BEGIN

@interface Theme : NSObject

- (instancetype)initWithProperties:(NSDictionary<NSString *, id> *)props;
- (NSDictionary<NSString *, id> *)properties;

+ (instancetype)presetThemeNamed:(NSString *)name;
+ (NSArray<NSString *> *)presetNames;
- (NSString *)presetName;

@property (nonatomic, readonly) UIColor *foregroundColor;
@property (nonatomic, readonly) UIColor *backgroundColor;
@property (readonly) UIKeyboardAppearance keyboardAppearance;
@property (readonly) UIStatusBarStyle statusBarStyle;

@end
extern NSString *const kThemeForegroundColor;
extern NSString *const kThemeBackgroundColor;

@interface UserPreferences : NSObject

@property CapsLockMapping capsLockMapping;
@property OptionMapping optionMapping;
@property BOOL backtickMapEscape;
@property BOOL hideExtraKeysWithExternalKeyboard;
@property BOOL overrideControlSpace;
@property BOOL hideStatusBar;
@property (nonatomic) Theme *theme;
@property BOOL shouldDisableDimming;
@property NSString *fontFamily;
@property NSNumber *fontSize;
@property NSArray<NSString *> *launchCommand;
@property NSArray<NSString *> *bootCommand;

+ (instancetype)shared;

- (BOOL)hasChangedLaunchCommand;

@end

extern NSString *const kPreferenceLaunchCommandKey;
extern NSString *const kPreferenceBootCommandKey;

NS_ASSUME_NONNULL_END
