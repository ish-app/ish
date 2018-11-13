//
//  UserPreferences.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import "UserPreferences.h"

static NSString *const kPreferenceMapCapsLockAsControlKey = @"kPreferenceMapCapsLockAsControlKey";
static NSString *const kPreferenceFontSizeKey = @"kPreferenceFontSizeKey";
static NSString *const kPreferenceForegroundColorKey = @"kPreferenceForegroundColorKey";
static NSString *const kPreferenceBackgroundColorKey = @"kPreferenceBackgroundColorKey";

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

- (NSString *)foregroundColor
{
    return [_defaults stringForKey:kPreferenceForegroundColorKey] ?: @"black";
}

- (void)setForegroundColor:(NSString *)foregroundColor
{
    [_defaults setObject:foregroundColor forKey:kPreferenceForegroundColorKey];
}

- (NSString *)backgroundColor
{
    return [_defaults stringForKey:kPreferenceBackgroundColorKey] ?: @"white";
}

- (void)setBackgroundColor:(NSString *)backgroundColor
{
    [_defaults setObject:backgroundColor forKey:kPreferenceBackgroundColorKey];
}

- (NSString *)JSONDictionary
{
    NSDictionary *dict = @{
                           @"mapCapsLockAsControl": @(self.mapCapsLockAsControl),
                           @"fontSize": self.fontSize,
                           @"foregroundColor": self.foregroundColor,
                           @"backgroundColor": self.backgroundColor
                           };
    return [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:dict options:0 error:nil] encoding:NSUTF8StringEncoding];
}

@end
