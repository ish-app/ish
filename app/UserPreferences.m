//
//  UserPreferences.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <UIKit/UIKit.h>
#import "UserPreferences.h"
#import "fs/proc/ish.h"

// IMPORTANT: If you add a constant here and expose it via UserPreferences,
// consider if it also needs to be exposed as a friendly preference and included
// in the KVO list below. (In most circumstances, the answer is "yes".)
static NSString *const kPreferenceCapsLockMappingKey = @"Caps Lock Mapping";
static NSString *const kPreferenceOptionMappingKey = @"Option Mapping";
static NSString *const kPreferenceBacktickEscapeKey = @"Backtick Mapping Escape";
static NSString *const kPreferenceHideExtraKeysWithExternalKeyboardKey = @"Hide Extra Keys With External Keyboard";
static NSString *const kPreferenceOverrideControlSpaceKey = @"Override Control Space";
static NSString *const kPreferenceFontFamilyKey = @"Font Family";
static NSString *const kPreferenceFontSizeKey = @"Font Size";
static NSString *const kPreferenceThemeKey = @"Theme";
static NSString *const kPreferenceDisableDimmingKey = @"Disable Dimming";
NSString *const kPreferenceLaunchCommandKey = @"Init Command";
NSString *const kPreferenceBootCommandKey = @"Boot Command";
NSString *const kPreferenceHideStatusBarKey = @"Status Bar";

NSDictionary<NSString *, NSString *> *friendlyPreferenceMapping;
NSDictionary<NSString *, NSString *> *friendlyPreferenceReverseMapping;
NSDictionary<NSString *, NSString *> *kvoProperties;

char **get_all_defaults_keys_impl(void) {
    NSArray<NSString *> *preferenceKeys = NSUserDefaults.standardUserDefaults.dictionaryRepresentation.allKeys;
    char **entries = malloc((preferenceKeys.count + 1) * sizeof(*entries));
    for (NSUInteger i = 0; i < preferenceKeys.count; ++i)
        entries[i] = strdup(preferenceKeys[i].UTF8String);
    entries[preferenceKeys.count] = NULL;
    return entries;
}

char *get_friendly_name_impl(const char *name) {
    const char *friendly_name = friendlyPreferenceReverseMapping[[NSString stringWithUTF8String:name]].UTF8String;
    if (friendly_name == NULL)
        return NULL;
    return strdup(friendly_name);
}

char *get_underlying_name_impl(const char *name) {
    return strdup(friendlyPreferenceMapping[[NSString stringWithUTF8String:name]].UTF8String);
}

bool get_user_default_impl(const char *name, char **buffer, size_t *size) {
    id value = [NSUserDefaults.standardUserDefaults objectForKey:[NSString stringWithUTF8String:name]];
    // Since we are writing with fragments, wrap the object in an array to have
    // a top-level object to check.
    if (!value || ![NSJSONSerialization isValidJSONObject:@[value]]) {
        return false;
    }
    NSError *error;
    NSJSONWritingOptions options = NSJSONWritingFragmentsAllowed | NSJSONWritingSortedKeys | NSJSONWritingPrettyPrinted;
    if (@available(iOS 13.0, *)) {
        options |= NSJSONWritingWithoutEscapingSlashes;
    }
    NSData *data = [NSJSONSerialization dataWithJSONObject:value options:options error:&error];
    if (error) {
        return false;
    }
    *buffer = malloc(data.length + 1);
    memcpy(*buffer, data.bytes, data.length);
    (*buffer)[data.length] = '\n';
    *size = data.length + 1;
    return true;
}

bool set_user_default_impl(const char *name, char *buffer, size_t size) {
    NSString *key = [NSString stringWithUTF8String:name];
    NSData *data = [NSData dataWithBytesNoCopy:buffer length:size freeWhenDone:NO];
    NSError *error;
    id value = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingFragmentsAllowed error:&error];
    if (error) {
        return false;
    }
    NSString *property = kvoProperties[key];
    if (property) {
        if ([UserPreferences.shared validateValue:&value forKey:property error:nil]) {
            [UserPreferences.shared setValue:value forKey:property];
        } else {
            return false;
        }
    } else {
        [NSUserDefaults.standardUserDefaults setValue:value forKey:key];
    }
    return true;
}

bool remove_user_default_impl(const char *name) {
    NSString *key = [NSString stringWithUTF8String:name];
    NSString *property = kvoProperties[key];
    if (property) {
        [UserPreferences.shared willChangeValueForKey:property];
    }
    [NSUserDefaults.standardUserDefaults removeObjectForKey:key];
    if (property) {
        [UserPreferences.shared didChangeValueForKey:property];
    }
    return true;
}

// TODO: Move these to Linux
#if ISH_LINUX
char **(*get_all_defaults_keys)(void);
char *(*get_friendly_name)(const char *name);
char *(*get_underlying_name)(const char *name);
bool (*get_user_default)(const char *name, char **buffer, size_t *size);
bool (*set_user_default)(const char *name, char *buffer, size_t size);
bool (*remove_user_default)(const char *name);
#endif

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
        [_defaults registerDefaults:@{
            kPreferenceFontFamilyKey: @"Menlo",
            kPreferenceFontSizeKey: @(12),
            kPreferenceThemeKey: defaultTheme.properties,
            kPreferenceCapsLockMappingKey: @(CapsLockMapControl),
            kPreferenceOptionMappingKey: @(OptionMapNone),
            kPreferenceBacktickEscapeKey: @(NO),
            kPreferenceDisableDimmingKey: @(NO),
            kPreferenceLaunchCommandKey: @[@"/bin/login", @"-f", @"root"],
            kPreferenceBootCommandKey: @[@"/sbin/init"],
            kPreferenceHideStatusBarKey: @(NO),
        }];
        _theme = [[Theme alloc] initWithProperties:[_defaults objectForKey:kPreferenceThemeKey]];
        get_all_defaults_keys = get_all_defaults_keys_impl;
        get_friendly_name = get_friendly_name_impl;
        get_underlying_name = get_underlying_name_impl;
        get_user_default = get_user_default_impl;
        set_user_default = set_user_default_impl;
        remove_user_default = remove_user_default_impl;
        friendlyPreferenceMapping = @{
            @"caps_lock_mapping": kPreferenceCapsLockMappingKey,
            @"option_mapping": kPreferenceOptionMappingKey,
            @"backtick_mapping_escape": kPreferenceBacktickEscapeKey,
            @"hide_extra_keys_with_external_keyboard": kPreferenceHideExtraKeysWithExternalKeyboardKey,
            @"override_control_space": kPreferenceOverrideControlSpaceKey,
            @"font_family": kPreferenceFontFamilyKey,
            @"font_size": kPreferenceFontSizeKey,
            @"disable_dimming": kPreferenceDisableDimmingKey,
            @"launch_command": kPreferenceLaunchCommandKey,
            @"boot_command": kPreferenceBootCommandKey,
            @"hide_status_bar": kPreferenceHideStatusBarKey,
        };
        NSMutableDictionary <NSString *, NSString *> *reverseMapping = [NSMutableDictionary new];
        for (NSString *key in friendlyPreferenceMapping) {
            reverseMapping[friendlyPreferenceMapping[key]] = key;
        }
        friendlyPreferenceReverseMapping = reverseMapping;
        // Helps a bit with compile-time safety and autocompletion
#define property(x) NSStringFromSelector(@selector(x))
        kvoProperties = @{
            kPreferenceCapsLockMappingKey: property(capsLockMapping),
            kPreferenceOptionMappingKey: property(optionMapping),
            kPreferenceBacktickEscapeKey: property(backtickMapEscape),
            kPreferenceHideExtraKeysWithExternalKeyboardKey: property(hideExtraKeysWithExternalKeyboard),
            kPreferenceOverrideControlSpaceKey: property(overrideControlSpace),
            kPreferenceFontFamilyKey: property(fontFamily),
            kPreferenceFontSizeKey: property(fontSize),
            kPreferenceDisableDimmingKey: property(shouldDisableDimming),
            kPreferenceLaunchCommandKey: property(launchCommand),
            kPreferenceBootCommandKey: property(bootCommand),
            kPreferenceHideStatusBarKey: property(hideStatusBar),
        };
#undef property
    }
    return self;
}

// MARK: - Preference properties

// MARK: capsLockMapping
- (CapsLockMapping)capsLockMapping {
    return [_defaults integerForKey:kPreferenceCapsLockMappingKey];
}

- (void)setCapsLockMapping:(CapsLockMapping)capsLockMapping {
    [_defaults setInteger:capsLockMapping forKey:kPreferenceCapsLockMappingKey];
}

- (BOOL)validateCapsLockMapping:(id *)value error:(NSError **)error {
    if (![*value isKindOfClass:NSNumber.class]) {
        return NO;
    }
    int _value = [(NSNumber *)(*value) intValue];
    return _value >= __CapsLockMapLast && value < __CapsLockMapFirst;
}

// MARK: optionMapping
- (OptionMapping)optionMapping {
    return [_defaults integerForKey:kPreferenceOptionMappingKey];
}

- (void)setOptionMapping:(OptionMapping)optionMapping {
    [_defaults setInteger:optionMapping forKey:kPreferenceOptionMappingKey];
}

- (BOOL)validateOptionMapping:(id *)value error:(NSError **)error {
    if (![*value isKindOfClass:NSNumber.class]) {
        return NO;
    }
    int _value = [(NSNumber *)(*value) intValue];
    return _value >= __OptionMapFirst && value < __OptionMapFirst;
}

// MARK: backtickMapEscape
- (BOOL)backtickMapEscape {
    return [_defaults boolForKey:kPreferenceBacktickEscapeKey];
}

- (void)setBacktickMapEscape:(BOOL)backtickMapEscape {
    [_defaults setBool:backtickMapEscape forKey:kPreferenceBacktickEscapeKey];
}

- (BOOL)validateBacktickMapEscape:(id *)value error:(NSError **)error {
    return [*value isKindOfClass:NSNumber.class];
}

// MARK: hideExtraKeysWithExternalKeyboard
- (BOOL)hideExtraKeysWithExternalKeyboard {
    return [_defaults boolForKey:kPreferenceHideExtraKeysWithExternalKeyboardKey];
}

- (void)setHideExtraKeysWithExternalKeyboard:(BOOL)hideExtraKeysWithExternalKeyboard {
    [_defaults setBool:hideExtraKeysWithExternalKeyboard forKey:kPreferenceHideExtraKeysWithExternalKeyboardKey];
}

- (BOOL)validateHideExtraKeysWithExternalKeyboard:(id *)value error:(NSError **)error {
    return [*value isKindOfClass:NSNumber.class];
}

// MARK: overrideControlSpace
- (BOOL)overrideControlSpace {
    return [_defaults boolForKey:kPreferenceOverrideControlSpaceKey];
}

- (void)setOverrideControlSpace:(BOOL)overrideControlSpace {
    [_defaults setBool:overrideControlSpace forKey:kPreferenceOverrideControlSpaceKey];
}

- (BOOL)validateOverrideControlSpace:(id *)value error:(NSError **)error {
    return [*value isKindOfClass:NSNumber.class];
}

// MARK: fontSize
- (NSNumber *)fontSize {
    return [_defaults objectForKey:kPreferenceFontSizeKey];
}

- (void)setFontSize:(NSNumber *)fontSize {
    [_defaults setObject:fontSize forKey:kPreferenceFontSizeKey];
}

- (BOOL)validateFontSize:(id *)value error:(NSError **)error {
    return [*value isKindOfClass:NSNumber.class];
}

// MARK: fontFamily
- (NSString *)fontFamily {
    return [_defaults objectForKey:kPreferenceFontFamilyKey];
}

- (void)setFontFamily:(NSString *)fontFamily {
    [_defaults setObject:fontFamily forKey:kPreferenceFontFamilyKey];
}

- (BOOL)validateFontFamily:(id *)value error:(NSError **)error {
    return [*value isKindOfClass:NSString.class];
}

// MARK: theme
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

// MARK: shouldDisableDimming
- (BOOL)shouldDisableDimming {
    return [_defaults boolForKey:kPreferenceDisableDimmingKey];
}

- (void)setShouldDisableDimming:(BOOL)dim {
    [_defaults setBool:dim forKey:kPreferenceDisableDimmingKey];
}

- (BOOL)validateShouldDisableDimming:(id *)value error:(NSError **)error {
    return [*value isKindOfClass:NSNumber.class];
}

// MARK: launchCommand
- (NSArray<NSString *> *)launchCommand {
    return [_defaults stringArrayForKey:kPreferenceLaunchCommandKey];
}

- (void)setLaunchCommand:(NSArray<NSString *> *)launchCommand {
    [_defaults setObject:launchCommand forKey:kPreferenceLaunchCommandKey];
}

- (BOOL)validateLaunchCommand:(id *)value error:(NSError **)error {
    if (![*value isKindOfClass:NSArray.class]) {
        return NO;
    }
    for (id element in (NSArray *)(*value)) {
        if (![element isKindOfClass:NSString.class]) {
            return NO;
        }
    }
    return YES;
}

- (BOOL)hasChangedLaunchCommand {
    NSArray *defaultLaunchCommand = [[[NSUserDefaults alloc] initWithSuiteName:NSRegistrationDomain] stringArrayForKey:kPreferenceLaunchCommandKey];
    return ![self.launchCommand isEqual:defaultLaunchCommand];
}

// MARK: bootCommand
- (NSArray<NSString *> *)bootCommand {
    return [_defaults stringArrayForKey:kPreferenceBootCommandKey];
}

- (void)setBootCommand:(NSArray<NSString *> *)bootCommand {
    [_defaults setObject:bootCommand forKey:kPreferenceBootCommandKey];
}

- (BOOL)validateBootCommand:(id *)value error:(NSError **)error {
    if (![*value isKindOfClass:NSArray.class]) {
        return NO;
    }
    for (id element in (NSArray *)(*value)) {
        if (![element isKindOfClass:NSString.class]) {
            return NO;
        }
    }
    return YES;
}

// MARK: hideStatusBar
- (BOOL)hideStatusBar {
    return [_defaults boolForKey:kPreferenceHideStatusBarKey];
}

- (void)setHideStatusBar:(BOOL)showStatusBar {
    [_defaults setBool:showStatusBar forKey:kPreferenceHideStatusBarKey];
}

- (BOOL)validateHideStatusBar:(id *)value error:(NSError **)error {
    return [*value isKindOfClass:NSNumber.class];
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
    if (lightness > 0.5) {
        if (@available(iOS 13, *))
            return UIStatusBarStyleDarkContent;
        else
            return UIStatusBarStyleDefault;
    } else {
        return UIStatusBarStyleLightContent;
    }
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
