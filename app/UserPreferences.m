//
//  UserPreferences.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <UIKit/UIKit.h>
#import "UserPreferences.h"
#import "UIColor+additions.h"
static NSString *const kPreferenceCapsLockMappingKey = @"Caps Lock Mapping";
static NSString *const kPreferenceOptionMappingKey = @"Option Mapping";
static NSString *const kPreferenceBacktickEscapeKey = @"Backtick Mapping Escape";
static NSString *const kPreferenceHideExtraKeysWithExternalKeyboard = @"Hide Extra Keys With External Keyboard";
static NSString *const kPreferenceOverrideControlSpace = @"Override Control Space";
static NSString *const kPreferenceFontFamilyKey = @"Font Family";
static NSString *const kPreferenceFontSizeKey = @"Font Size";
static NSString *const kPreferenceSchemeKey = @"Scheme";
static NSString *const kPreferenceSchemeDictKey = @"SchemeArray";
static NSString *const kPreferenceDefaultSchemeName = @"Light";
static NSString *const kPreferenceDisableDimmingKey = @"Disable Dimming";
NSString *const kPreferenceLaunchCommandKey = @"Init Command";
NSString *const kPreferenceBootCommandKey = @"Boot Command";

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
        NSMutableDictionary<NSString *, id> *modifiedPresets = (NSMutableDictionary<NSString *, id> *) [[NSMutableDictionary alloc] init];
        NSEnumerator *enumerator = [Scheme.presets keyEnumerator];
        id key; // This should be a string right?
        while((key = [enumerator nextObject])) {
            [modifiedPresets setObject:Scheme.presets[key].properties forKey:key];
        }
        
        [_defaults registerDefaults:@{
            kPreferenceFontFamilyKey: @"Menlo",
            kPreferenceFontSizeKey: @(12),
            kPreferenceSchemeKey: kPreferenceDefaultSchemeName,
            kPreferenceSchemeDictKey: [NSDictionary dictionaryWithDictionary:modifiedPresets],
            kPreferenceCapsLockMappingKey: @(CapsLockMapControl),
            kPreferenceOptionMappingKey: @(OptionMapNone),
            kPreferenceBacktickEscapeKey: @(NO),
            kPreferenceDisableDimmingKey: @(NO),
            kPreferenceLaunchCommandKey: @[@"/bin/login", @"-f", @"root"],
            kPreferenceBootCommandKey: @[@"/sbin/init"],
        }];
        NSString *currentSchemeName = [_defaults stringForKey:kPreferenceSchemeKey];
        _scheme = [self schemeFromName:currentSchemeName];
    }
    return self;
}

- (NSArray<NSString *> *)allSchemeNames {
    return [(NSDictionary<NSString *, id> *) [_defaults objectForKey:kPreferenceSchemeDictKey] allKeys];
}

- (CapsLockMapping)capsLockMapping {
    return [_defaults integerForKey:kPreferenceCapsLockMappingKey];
}
- (void)setCapsLockMapping:(CapsLockMapping)capsLockMapping {
    [_defaults setInteger:capsLockMapping forKey:kPreferenceCapsLockMappingKey];
}

- (OptionMapping)optionMapping {
    return [_defaults integerForKey:kPreferenceOptionMappingKey];
}
- (void)setOptionMapping:(OptionMapping)optionMapping {
    [_defaults setInteger:optionMapping forKey:kPreferenceOptionMappingKey];
}

- (BOOL)backtickMapEscape {
    return [_defaults boolForKey:kPreferenceBacktickEscapeKey];
}
- (void)setBacktickMapEscape:(BOOL)backtickMapEscape {
    [_defaults setBool:backtickMapEscape forKey:kPreferenceBacktickEscapeKey];
}

- (BOOL)hideExtraKeysWithExternalKeyboard {
    return [_defaults boolForKey:kPreferenceHideExtraKeysWithExternalKeyboard];
}
- (void)setHideExtraKeysWithExternalKeyboard:(BOOL)hideExtraKeysWithExternalKeyboard {
    [_defaults setBool:hideExtraKeysWithExternalKeyboard forKey:kPreferenceHideExtraKeysWithExternalKeyboard];
}
- (BOOL)overrideControlSpace {
    return [_defaults boolForKey:kPreferenceOverrideControlSpace];
}
- (void)setOverrideControlSpace:(BOOL)overrideControlSpace {
    [_defaults setBool:overrideControlSpace forKey:kPreferenceOverrideControlSpace];
}
- (NSNumber *)fontSize {
    return [_defaults objectForKey:kPreferenceFontSizeKey];
}
- (void)setFontSize:(NSNumber *)fontSize {
    [_defaults setObject:fontSize forKey:kPreferenceFontSizeKey];
}

- (NSString *)fontFamily {
    return [_defaults objectForKey:kPreferenceFontFamilyKey];
}
- (void)setFontFamily:(NSString *)fontFamily {
    [_defaults setObject:fontFamily forKey:kPreferenceFontFamilyKey];
}

- (UIColor *)foregroundColor {
    return self.scheme.foregroundColor;
}
- (UIColor *)backgroundColor {
    return self.scheme.backgroundColor;
}

- (void)setScheme:(Scheme *)scheme {
    _scheme = scheme;
    [_defaults setObject:scheme.name forKey:kPreferenceSchemeKey];
}

- (void)setSchemeToName:(NSString *)name {
    [self setScheme:[[Scheme alloc] initWithProperties:[_defaults objectForKey:kPreferenceSchemeDictKey][name]]];
}

- (Scheme *)schemeFromName:(NSString *)name {
    return [[Scheme alloc] initWithProperties:[_defaults objectForKey:kPreferenceSchemeDictKey][name]];
}

- (void) deleteScheme:(NSString *)name {
    NSMutableDictionary<NSString *, id> *dict = [[_defaults dictionaryForKey:kPreferenceSchemeDictKey] mutableCopy];
    [dict removeObjectForKey:name];
    [_defaults setObject:dict forKey:kPreferenceSchemeDictKey];
}

- (void) modifyScheme:(NSString *)name properties:(id)props {
    NSMutableDictionary<NSString *, id> *dict = [[_defaults dictionaryForKey:kPreferenceSchemeDictKey] mutableCopy];
    [dict setValue:props forKey:name];
    [_defaults setObject:dict forKey:kPreferenceSchemeDictKey];
    if (name == _scheme.name)
        [self setSchemeToName:name]; // Just to call sane KVO
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

@end

static id ArchiveColor(UIColor *color) {
    return [UIColor hexWithColor:color];
}
static UIColor *UnarchiveColor(id data) {
    return [UIColor colorWithHexString:data];
}

//MARK: Scheme Implementation
@implementation Scheme

- (instancetype)initWithProperties:(NSDictionary<NSString *,id> *)props {
    if (self = [super init]) {
        _foregroundColor = UnarchiveColor(props[kSchemeForegroundColor]);
        _backgroundColor = UnarchiveColor(props[kSchemeBackgroundColor]);
        _name = props[kSchemeName];
        _palette = props[kSchemePalette];
    }
    return self;
}

+ (instancetype)_schemeWithForegroundColor:(UIColor *)foreground backgroundColor:(UIColor *)background name:(NSString *)name palette:(NSArray<NSString *> *)palette {
    return [[self alloc] initWithProperties:@{kSchemeForegroundColor: ArchiveColor(foreground),
                                              kSchemeBackgroundColor: ArchiveColor(background),
                                              kSchemeName: name,
                                              kSchemePalette: palette
    }];
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
    return @{kSchemeForegroundColor: ArchiveColor(self.foregroundColor),
             kSchemeBackgroundColor: ArchiveColor(self.backgroundColor),
             kSchemeName: self.name,
             kSchemePalette: self.palette
    };
}

- (BOOL)isEqual:(id)object {
    if ([self class] != [object class])
        return NO;
    return [self.properties isEqualToDictionary:[object properties]];
}

+ (NSArray<NSString *> *)schemeNames {
    return [[[UserPreferences shared] allSchemeNames] sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
}

+ (NSDictionary<NSString *, Scheme *> *)presets {
    return @{
         @"Light": [self _schemeWithForegroundColor:UIColor.blackColor
                                   backgroundColor:UIColor.whiteColor
                                              name:@"Light"
                                           palette:@[]],
         @"Dark":  [self _schemeWithForegroundColor:UIColor.whiteColor
                                   backgroundColor:UIColor.blackColor
                                              name: @"Dark"
                                           palette:@[]],
         @"1337":  [self _schemeWithForegroundColor:UIColor.greenColor
                                   backgroundColor:UIColor.blackColor
                                              name:@"1337"
                                           palette:@[]]
    };
}

+ (instancetype)schemeNamed:(NSString *)name {
    return [[UserPreferences shared] schemeFromName:name];
}

- (NSString *)presetName {
    for (NSString *name in Scheme.presets) {
        if ([self isEqual:Scheme.presets[name]])
            return name;
    }
    return nil;
}

@end

NSString *const kSchemeForegroundColor = @"ForegroundColor";
NSString *const kSchemeBackgroundColor = @"BackgroundColor";
NSString *const kSchemeName = @"Name";
NSString *const kSchemePalette = @"Palette";
