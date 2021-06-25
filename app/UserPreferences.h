/*
 *   Copyright (c) 2021 c0dine
 *   All rights reserved.
 *   Feel free to contribute!
 */
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

enum {
    PaletteBlackColor,
    PaletteRedColor,
    PaletteGreenColor,
    PaletteYellowColor,
    PaletteBlueColor,
    PaletteMagentaColor,
    PaletteCyanColor,
    PaletteWhiteColor,
    
    // Background
    PaletteBlackBackgroundColor,
    PaletteRedBackgroundColor,
    PaletteGreenBackgroundColor,
    PaletteYellowBackgroundColor,
    PaletteBlueBackgroundColor,
    PaletteMagentaBackgroundColor,
    PaletteCyanBackgroundColor,
    PaletteWhiteBackgroundColor,
};

NS_ASSUME_NONNULL_BEGIN

@interface Scheme : NSObject

- (instancetype)initWithProperties:(NSDictionary<NSString *, id> *)props;
- (NSDictionary<NSString *, id> *)properties;

+ (NSDictionary<NSString *, Scheme *> *)presets;
+ (NSArray<NSString *> *)schemeNames;
- (NSString *)presetName;

@property (nonatomic, readonly) UIColor *foregroundColor;
@property (nonatomic, readonly) UIColor *backgroundColor;
@property (nonatomic, readonly) NSArray<NSString *> *palette;
@property (nonatomic) NSString *name;
@property (readonly) UIKeyboardAppearance keyboardAppearance;
@property (readonly) UIStatusBarStyle statusBarStyle;

@end
extern NSString *const kSchemeForegroundColor;
extern NSString *const kSchemeBackgroundColor;
extern NSString *const kSchemeName;
extern NSString *const kSchemePalette;
@interface UserPreferences : NSObject

@property CapsLockMapping capsLockMapping;
@property OptionMapping optionMapping;
@property BOOL backtickMapEscape;
@property BOOL hideExtraKeysWithExternalKeyboard;
@property BOOL overrideControlSpace;
@property (nonatomic) Scheme *scheme;
@property BOOL hideStatusBar;
@property BOOL shouldDisableDimming;
@property NSString *fontFamily;
@property NSNumber *fontSize;
@property NSArray<NSString *> *launchCommand;
@property NSArray<NSString *> *bootCommand;

+ (instancetype)shared;

- (BOOL)hasChangedLaunchCommand;
- (void)setSchemeToName:(NSString *)name;
- (Scheme *)schemeFromName:(NSString *)name;
- (void)deleteScheme:(NSString *)name;
- (void)modifyScheme:(NSString *)name properties:(id)props;
@end

extern NSString *const kPreferenceLaunchCommandKey;
extern NSString *const kPreferenceBootCommandKey;

NS_ASSUME_NONNULL_END
