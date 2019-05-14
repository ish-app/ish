//
//  UserPreferences.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <UIKit/UIKit.h>
#import "UserPreferences.h"

static NSString *const kPreferenceCapsLockMappingKey = @"Caps Lock Mapping";
static NSString *const kPreferenceFontSizeKey = @"Font Size";
static NSString *const kPreferenceThemeKey = @"Theme";
static NSString *const kPreferenceDisableDimmingKey = @"Disable Dimming";
static NSString *const kPreferenceLaunchCommandKey = @"Init Command";
static NSString *const kPreferenceBootCommandKey = @"Boot Command";
static NSString *const kPreferenceBootEnabledKey = @"Boot Enabled";

@implementation UserPreferences {
    NSUserDefaults *_defaults;
}

+ (instancetype)shared {
    static UserPreferences *shared = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        shared = [[self alloc] init];
    });
    return shared;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _defaults = [NSUserDefaults standardUserDefaults];
        Theme *defaultTheme = [Theme presetThemeNamed:@"Light"];
        [_defaults registerDefaults:@{kPreferenceFontSizeKey: @(12),
                                      kPreferenceThemeKey: defaultTheme.properties,
                                      kPreferenceCapsLockMappingKey: @(CapsLockMapControl),
                                      kPreferenceDisableDimmingKey: @(NO),
                                      kPreferenceLaunchCommandKey: @[@"/bin/login", @"-f", @"root"],
                                      kPreferenceBootCommandKey: @[@"/sbin/init"],
                                      kPreferenceBootEnabledKey: @(YES),
                                      }];
        _theme = [[Theme alloc] initWithProperties:[_defaults objectForKey:kPreferenceThemeKey]];
    }
    return self;
}

- (CapsLockMapping)capsLockMapping {
    return [_defaults integerForKey:kPreferenceCapsLockMappingKey];
}
- (void)setCapsLockMapping:(CapsLockMapping)capsLockMapping {
    [_defaults setInteger:capsLockMapping forKey:kPreferenceCapsLockMappingKey];
}

- (NSNumber *)fontSize {
    return [_defaults objectForKey:kPreferenceFontSizeKey];
}
- (void)setFontSize:(NSNumber *)fontSize {
    [_defaults setObject:fontSize forKey:kPreferenceFontSizeKey];
}

- (UIColor *)foregroundColor {
    return self.theme.foregroundColor;
}
- (UIColor *)backgroundColor {
    return self.theme.backgroundColor;
}

- (void)setTheme:(Theme *)theme {
    _theme = theme;
    [_defaults setObject:theme.properties forKey:kPreferenceThemeKey];
}

- (BOOL)shouldDisableDimming {
    return [_defaults boolForKey:kPreferenceDisableDimmingKey];
}
- (void)setShouldDisableDimming:(BOOL)dim {
    [_defaults setBool:dim forKey:kPreferenceDisableDimmingKey];
}

- (NSArray<NSString *> *)launchCommand {
    return [_defaults stringArrayForKey:kPreferenceLaunchCommandKey];
}
- (void)setLaunchCommand:(NSArray<NSString *> *)launchCommand {
    [_defaults setObject:launchCommand forKey:kPreferenceLaunchCommandKey];
}
- (BOOL)hasChangedLaunchCommand {
    NSArray *defaultLaunchCommand = [[[NSUserDefaults alloc] initWithSuiteName:NSRegistrationDomain] stringArrayForKey:kPreferenceLaunchCommandKey];
    return ![self.launchCommand isEqual:defaultLaunchCommand];
}

- (NSArray<NSString *> *)bootCommand {
    return [_defaults stringArrayForKey:kPreferenceBootCommandKey];
}
- (void)setBootCommand:(NSArray<NSString *> *)bootCommand {
    [_defaults setObject:bootCommand forKey:kPreferenceBootCommandKey];
}

- (BOOL)bootEnabled {
    return [_defaults boolForKey:kPreferenceBootEnabledKey];
}
- (void)setBootEnabled:(BOOL)bootEnabled {
    [_defaults setBool:bootEnabled forKey:kPreferenceBootEnabledKey];
}

@end

static id ArchiveColor(UIColor *color) {
    CGFloat r, g, b;
    [color getRed:&r green:&g blue:&b alpha:nil];
    return [NSString stringWithFormat:@"%f %f %f", r, g, b];
}
static UIColor *UnarchiveColor(id data) {
    NSArray<NSString *> *components = [data componentsSeparatedByString:@" "];
    CGFloat r = components[0].doubleValue;
    CGFloat g = components[1].doubleValue;
    CGFloat b = components[2].doubleValue;
    return [UIColor colorWithRed:r green:g blue:b alpha:1];
}

@implementation Theme

- (instancetype)initWithProperties:(NSDictionary<NSString *,id> *)props {
    if (self = [super init]) {
        _foregroundColor = UnarchiveColor(props[kThemeForegroundColor]);
        _backgroundColor = UnarchiveColor(props[kThemeBackgroundColor]);
    }
    return self;
}

+ (instancetype)_themeWithForegroundColor:(UIColor *)foreground backgroundColor:(UIColor *)background {
    return [[self alloc] initWithProperties:@{kThemeForegroundColor: ArchiveColor(foreground),
                                              kThemeBackgroundColor: ArchiveColor(background)}];
}

- (UIStatusBarStyle)statusBarStyle {
    CGFloat lightness;
    [self.backgroundColor getWhite:&lightness alpha:nil];
    if (lightness > 0.5)
        return UIStatusBarStyleDefault;
    else
        return UIStatusBarStyleLightContent;
}

- (UIKeyboardAppearance)keyboardAppearance {
    CGFloat lightness;
    [self.backgroundColor getWhite:&lightness alpha:nil];
    if (lightness > 0.5)
        return UIKeyboardAppearanceLight;
    else
        return UIKeyboardAppearanceDark;
}

- (NSDictionary<NSString *,id> *)properties {
    return @{kThemeForegroundColor: ArchiveColor(self.foregroundColor),
             kThemeBackgroundColor: ArchiveColor(self.backgroundColor)};
}

- (BOOL)isEqual:(id)object {
    if ([self class] != [object class])
        return NO;
    return [self.properties isEqualToDictionary:[object properties]];
}

NSDictionary<NSString *, Theme *> *presetThemes;
+ (void)initialize {
    presetThemes = @{@"Light": [self _themeWithForegroundColor:UIColor.blackColor
                                               backgroundColor:UIColor.whiteColor],
                     @"Dark":  [self _themeWithForegroundColor:UIColor.whiteColor
                                               backgroundColor:UIColor.blackColor],
                     @"1337":  [self _themeWithForegroundColor:UIColor.greenColor
                                               backgroundColor:UIColor.blackColor]};
}

+ (NSArray<NSString *> *)presetNames {
    return @[@"Light", @"Dark", @"1337"];
}
+ (instancetype)presetThemeNamed:(NSString *)name {
    return presetThemes[name];
}
- (NSString *)presetName {
    for (NSString *name in presetThemes) {
        if ([self isEqual:presetThemes[name]])
            return name;
    }
    return nil;
}

@end

NSString *const kThemeForegroundColor = @"ForegroundColor";
NSString *const kThemeBackgroundColor = @"BackgroundColor";
