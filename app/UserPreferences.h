//
//  UserPreferences.h
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, CapsLockMapping) {
    CapsLockMapNone = 0,
    CapsLockMapControl,
    CapsLockMapEscape,
};

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
@property (nonatomic) Theme *theme;
@property BOOL shouldDisableDimming;
@property NSNumber *fontSize;
@property NSArray<NSString *> *launchCommand;
@property NSArray<NSString *> *bootCommand;
@property BOOL bootEnabled;

+ (instancetype)shared;

- (BOOL)hasChangedLaunchCommand;

@end

NS_ASSUME_NONNULL_END
