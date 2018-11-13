//
//  UserPreferences.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <UIKit/UIKit.h>
#import "UserPreferences.h"

static NSString *const kPreferenceMapCapsLockAsControlKey = @"kPreferenceMapCapsLockAsControlKey";
static NSString *const kPreferenceFontSizeKey = @"kPreferenceFontSizeKey";
static NSString *const kPreferenceThemeKey = @"kPreferenceThemeKey";

UIColor *ThemeForegroundColor(UserPreferenceTheme theme) {
    switch (theme) {
        case UserPreferenceThemeLight:
            return [UIColor blackColor];
        case UserPreferenceThemeDark:
            return [UIColor whiteColor];
        case UserPreferenceThemeCount:
            assert("invalid theme");
            return nil;
    }
}

UIColor *ThemeBackgroundColor(UserPreferenceTheme theme) {
    switch (theme) {
        case UserPreferenceThemeLight:
            return [UIColor whiteColor];
        case UserPreferenceThemeDark:
            return [UIColor blackColor];
        case UserPreferenceThemeCount:
            assert("invalid theme");
            return nil;
    }
}

UIStatusBarStyle ThemeStatusBar(UserPreferenceTheme theme) {
    switch (theme) {
        case UserPreferenceThemeLight:
            return UIStatusBarStyleDefault;
        case UserPreferenceThemeDark:
            return UIStatusBarStyleLightContent;
        case UserPreferenceThemeCount:
            assert("invalid theme");
            return UIStatusBarStyleDefault;
    }
}

NSString *ThemeName(UserPreferenceTheme theme) {
    switch (theme) {
        case UserPreferenceThemeLight:
            return @"Light";
        case UserPreferenceThemeDark:
            return @"Dark";
        case UserPreferenceThemeCount:
            assert("invalid theme");
            return nil;
    }
}

@implementation UserPreferences
{
    NSUserDefaults *_defaults;
}

+ (instancetype)shared
{
    static UserPreferences *shared = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        shared = [[self alloc] init];
    });
    return shared;
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        _defaults = [NSUserDefaults standardUserDefaults];
    }
    return self;
}

- (BOOL)mapCapsLockAsControl
{
    return [_defaults boolForKey:kPreferenceMapCapsLockAsControlKey] ?: NO;
}

- (void)setMapCapsLockAsControl:(BOOL)mapCapsLockAsControl
{
    [_defaults setBool:mapCapsLockAsControl forKey:kPreferenceMapCapsLockAsControlKey];
}

- (NSNumber *)fontSize
{
    return [_defaults objectForKey:kPreferenceFontSizeKey] ?: @(12);
}

- (void)setFontSize:(NSNumber *)fontSize
{
    [_defaults setObject:fontSize forKey:kPreferenceFontSizeKey];
}

- (UserPreferenceTheme)theme
{
    return [_defaults integerForKey:kPreferenceThemeKey] ?: UserPreferenceThemeLight;
}

- (void)setTheme:(UserPreferenceTheme)theme
{
    [_defaults setInteger:theme forKey:kPreferenceThemeKey];
}

- (NSString *)JSONDictionary
{
    NSDictionary *dict = @{
                           @"mapCapsLockAsControl": @(self.mapCapsLockAsControl),
                           @"fontSize": self.fontSize,
                           @"foregroundColor": [self _hexFromUIColor:ThemeForegroundColor(self.theme)],
                           @"backgroundColor": [self _hexFromUIColor:ThemeBackgroundColor(self.theme)]
                           };
    return [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:dict options:0 error:nil] encoding:NSUTF8StringEncoding];
}

-(NSString *)_hexFromUIColor:(UIColor *)color
{
    const CGFloat *components = CGColorGetComponents(color.CGColor);
    size_t count = CGColorGetNumberOfComponents(color.CGColor);
    
    if(count == 2){
        return [NSString stringWithFormat:@"#%02lX%02lX%02lX",
                lroundf(components[0] * 255.0),
                lroundf(components[0] * 255.0),
                lroundf(components[0] * 255.0)];
    } else {
        return [NSString stringWithFormat:@"#%02lX%02lX%02lX",
                lroundf(components[0] * 255.0),
                lroundf(components[1] * 255.0),
                lroundf(components[2] * 255.0)];
    }
}

@end
