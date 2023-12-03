//
//  UserPreferences.h
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <UIKit/UIKit.h>
#import "Theme.h"

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

typedef NS_ENUM(NSInteger, CursorStyle) {
    __CursorStyleFirst = 0,
    CursorStyleBlock = 0,
    CursorStyleBeam,
    CursorStyleUnderline,
    __CursorStyleLast,
};

typedef NS_ENUM(NSInteger, ColorScheme) {
    __ColorSchemeFirst = 0,
    ColorSchemeMatchSystem = 0,
    ColorSchemeAlwaysLight,
    ColorSchemeAlwaysDark,
    __ColorSchemeLast,
};

NS_ASSUME_NONNULL_BEGIN

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
@property (nonatomic) Palette *palette;
@property BOOL shouldDisableDimming;
@property (null_resettable) NSString *fontFamily;
@property (readonly) NSString *fontFamilyUserFacingName;
@property (readonly) UIFont *approximateFont;
@property NSNumber *fontSize;
@property ColorScheme colorScheme;
@property (readonly) BOOL requestingDarkAppearance;
@property (readonly) UIUserInterfaceStyle userInterfaceStyle API_AVAILABLE(ios(12.0));
@property (readonly) UIKeyboardAppearance keyboardAppearance;
@property CursorStyle cursorStyle;
@property (readonly) NSString *htermCursorShape;
@property BOOL blinkCursor;
@property (readonly) UIStatusBarStyle statusBarStyle;
@property NSArray<NSString *> *launchCommand;
@property NSArray<NSString *> *bootCommand;

+ (instancetype)shared;

- (BOOL)hasChangedLaunchCommand;

@end

extern NSString *const kPreferenceLaunchCommandKey;
extern NSString *const kPreferenceBootCommandKey;

NS_ASSUME_NONNULL_END
