//
//  Theme.m
//  iSH
//
//  Created by Saagar Jha on 2/25/22.
//

#import "Theme.h"
#import "UserPreferences.h"
#import "fs/proc/ish.h"

char *get_documents_directory_impl(void) {
    return strdup(NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject.UTF8String);
}

#define THEME_VERSION 1

@implementation UIColor (iSH)
- (nullable instancetype)ish_initWithHexString:(NSString *)string {
    if (![string hasPrefix:@"#"]) {
        return nil;
    }
    NSScanner *scanner = [NSScanner scannerWithString:string];
    // Skip the leading #
    [scanner setScanLocation:1];
    unsigned int value;
    if (![scanner scanHexInt:&value] || scanner.scanLocation != string.length) {
        return nil;
    }
    unsigned int red;
    unsigned int green;
    unsigned int blue;
    unsigned int alpha;
    if (string.length == 4) { // RGB
        blue = ((value & 0x00f) >> 0) * 0x11;
        green = ((value & 0x0f0) >> 4) * 0x11;
        red = ((value & 0xf00) >> 8) * 0x11;
        alpha = 0xff;
    } else if (string.length == 5) { // RGBA
        blue = ((value & 0x000f) >> 0) * 0x11;
        green = ((value & 0x00f0) >> 4) * 0x11;
        red = ((value & 0x0f00) >> 8) * 0x11;
        alpha = ((value & 0xf000) >> 12) * 0x11;
    } else if (string.length == 7) { // RRGGBB
        blue = (value & 0x0000ff) >> 0;
        green = (value & 0x00ff00) >> 8;
        red = (value & 0xff0000) >> 16;
        alpha = 0xff;
    } else if (string.length == 9) { // RRGGBBAA
        blue = (value & 0x000000ff) >> 0;
        green = (value & 0x0000ff00) >> 8;
        red = (value & 0x00ff0000) >> 16;
        alpha = (value & 0xff000000) >> 24;
    } else {
        return nil;
    }
    return [UIColor colorWithRed:1.0 * red / 0xff green:1.0 * green / 0xff blue:1.0 * blue / 0xff alpha:1.0 * alpha / 0xff];
}
@end

@interface DirectoryWatcher: NSObject<NSFilePresenter>
@property(readonly, copy) NSURL *presentedItemURL;
- (instancetype)initWithURL:(NSURL *)url handler:(void (^)(void))handler;
@end

@implementation DirectoryWatcher {
    void (^_handler)(void);
}
- (instancetype)initWithURL:(NSURL *)url handler:(void (^)(void))handler {
    if (self = [super init]) {
        self->_presentedItemURL = url;
        self->_handler = handler;
    }
    return self;
}

- (NSOperationQueue *)presentedItemOperationQueue {
    return NSOperationQueue.mainQueue;
}

- (void)presentedItemDidChange {
    self->_handler();
}
@end

@interface Palette ()
@property(readonly, nonnull) NSDictionary *serializedRepresentation;

- (nullable instancetype)initWithSerializedRepresentation:(nonnull NSDictionary *)serializedRepresentation;
@end

@implementation Palette

- (instancetype)initWithForegroundColor:(NSString *)foregroundColor backgroundColor:(NSString *)backgroundColor cursorColor:(NSString *)cursorColor colorPaletteOverrides:(NSArray<NSString *> *)colorPaletteOverrides {
    if (self = [super init]) {
        self->_foregroundColor = foregroundColor;
        self->_backgroundColor = backgroundColor;
        self->_cursorColor = cursorColor;
        self->_colorPaletteOverrides = colorPaletteOverrides;
    }
    return self;
}

- (instancetype)initWithSerializedRepresentation:(NSDictionary *)serializedRepresentation {
#define VALID_COLOR(color) (color && [color isKindOfClass:NSString.class] && [[UIColor alloc] ish_initWithHexString:color])
    id foregroundColor = serializedRepresentation[@"foregroundColor"];
    id backgroundColor = serializedRepresentation[@"backgroundColor"];
    id cursorColor = serializedRepresentation[@"cursorColor"];
    id colorPaletteOverrides = serializedRepresentation[@"colorPaletteOverrides"];
    BOOL validColorPalette = YES;
    if (colorPaletteOverrides) {
        if ([colorPaletteOverrides isKindOfClass:NSArray.class]) {
            for (id color in colorPaletteOverrides) {
                validColorPalette = validColorPalette || VALID_COLOR(color);
            }
        } else {
            validColorPalette = NO;
        }
    }
    if (VALID_COLOR(foregroundColor) && VALID_COLOR(backgroundColor) && (!cursorColor || VALID_COLOR(cursorColor)) && validColorPalette) {
        return [self initWithForegroundColor:foregroundColor backgroundColor:backgroundColor cursorColor:cursorColor colorPaletteOverrides:colorPaletteOverrides];
    } else {
        return nil;
    }
#undef VALID_COLOR
}

- (NSDictionary *)serializedRepresentation {
    NSMutableDictionary *representation = [@{
        @"foregroundColor": self.foregroundColor,
        @"backgroundColor": self.backgroundColor,
    } mutableCopy];
    if (self.cursorColor) {
        representation[@"cursorColor"] = self.cursorColor;
    }
    if (self.colorPaletteOverrides) {
        representation[@"colorPaletteOverrides"] = self.colorPaletteOverrides;
    }
    return  representation;
}

@end

@interface ThemeAppearance ()
@property(readonly, nonnull) NSDictionary *serializedRepresentation;

- (nullable instancetype)initWithSerializedRepresentation:(nonnull NSDictionary *)serializedRepresentation;
@end

@implementation ThemeAppearance

- (instancetype)initWithLightOverride:(BOOL)lightOverride darkOverride:(BOOL)darkOverride {
    if (self = [super init]) {
        self->_lightOverride = lightOverride;
        self->_darkOverride = darkOverride;
    }
    return self;
}

- (instancetype)initWithSerializedRepresentation:(NSDictionary *)serializedRepresentation {
    id lightOverride = serializedRepresentation[@"lightOverride"];
    id darkOverride = serializedRepresentation[@"darkOverride"];
    if ([lightOverride isKindOfClass:NSNumber.class] && [darkOverride isKindOfClass:NSNumber.class]) {
        return [self initWithLightOverride:[lightOverride boolValue] darkOverride:[darkOverride boolValue]];
    } else {
        return nil;
    }
}

+ (instancetype)alwaysLight {
    return [[self alloc] initWithLightOverride:NO darkOverride:YES];
}

+ (instancetype)alwaysDark {
    return [[self alloc] initWithLightOverride:YES darkOverride:NO];
}

- (NSDictionary *)serializedRepresentation {
    return @{
        @"lightOverride": @(self.lightOverride),
        @"darkOverride": @(self.darkOverride),
    };
}

@end

DirectoryWatcher *directoryWatcher;
NSString *const ThemesUpdatedNotification = @"ThemesUpdatedNotification";
NSString *const ThemeUpdatedNotification = @"ThemeUpdatedNotification";

@interface Theme ()
@property(readonly, nonnull) NSData *data;
@end

// TODO: Move these to Linux
#if ISH_LINUX
char *(*get_documents_directory)(void);
#endif

@implementation Theme
+ (void)initialize {
    directoryWatcher = [[DirectoryWatcher alloc] initWithURL:self.themesDirectory handler:^{
        [NSNotificationCenter.defaultCenter postNotificationName:ThemesUpdatedNotification object:nil];
    }];
    [NSFileCoordinator addFilePresenter:directoryWatcher];
    
    get_documents_directory = get_documents_directory_impl;
    [NSFileManager.defaultManager createDirectoryAtURL:self.themesDirectory withIntermediateDirectories:YES attributes:nil error:nil];
}

- (instancetype)initWithName:(NSString *)name palette:(Palette *)palette appearance:(ThemeAppearance *)appearance {
    Theme *theme = [self initWithName:name lightPalette:palette darkPalette:palette appearance:appearance];
    return theme;
}

- (instancetype)initWithName:(NSString *)name lightPalette:(nonnull Palette *)lightPalette darkPalette:(nonnull Palette *)darkPalette appearance:(nullable ThemeAppearance *)appearance {
    if (self = [super init]) {
        self->_name = name;
        self->_lightPalette = lightPalette;
        self->_darkPalette = darkPalette;
        self->_appearance = appearance;
    }
    return self;
}

- (nullable instancetype)initWithName:(NSString *)name data:(NSData *)data {
    id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    if (![json isKindOfClass:NSDictionary.class]) {
        return nil;
    }
    id version = json[@"version"];
    if (![version isKindOfClass:NSNumber.class] || ((NSNumber *)version).integerValue <= 0 || ((NSNumber *)version).integerValue > THEME_VERSION) {
        NSLog(@"Rejecting theme %@ with invalid version number", name);
        return nil;
    }
    id _appearance = json[@"appearance"];
    ThemeAppearance *appearance = [_appearance isKindOfClass:NSDictionary.class] ? [[ThemeAppearance alloc] initWithSerializedRepresentation:_appearance] : nil;
    id shared = json[@"shared"];
    id light = json[@"light"];
    id dark = json[@"dark"];
    if ([shared isKindOfClass:NSDictionary.class]) {
        Palette *palette = [[Palette alloc] initWithSerializedRepresentation:shared];
        return palette ? [self initWithName:name palette:palette appearance:appearance] : nil;
    } else if ([light isKindOfClass:NSDictionary.class] && [dark isKindOfClass:NSDictionary.class]) {
        Palette *lightPalette = [[Palette alloc] initWithSerializedRepresentation:light];
        Palette *darkPalette = [[Palette alloc] initWithSerializedRepresentation:dark];
        return lightPalette && darkPalette ? [self initWithName:name lightPalette:lightPalette darkPalette:darkPalette appearance:appearance] : nil;
    } else {
        NSLog(@"Rejecting theme %@ with invalid palette(s)", name);
        return nil;
    }
}

+ (NSArray<Theme *> *)defaultThemes {
    static NSArray<Theme *> *defaultThemes;
    if (!defaultThemes) {
        defaultThemes = @[
            [[self alloc] initWithName:@"Default"
                          lightPalette:[[Palette alloc] initWithForegroundColor:@"#000"
                                                                backgroundColor:@"#fff"
                                                                    cursorColor:nil
                                                          colorPaletteOverrides:nil]
                           darkPalette:[[Palette alloc] initWithForegroundColor:@"#fff"
                                                                backgroundColor:@"#000"
                                                                    cursorColor:nil
                                                          colorPaletteOverrides:nil]
                            appearance:nil],
            [[self alloc] initWithName:@"1337"
                               palette:[[Palette alloc] initWithForegroundColor:@"#0f0"
                                                                backgroundColor:@"#000"
                                                                    cursorColor:nil
                                                          colorPaletteOverrides:nil]
                            appearance:ThemeAppearance.alwaysDark],
            [[self alloc] initWithName:@"Solarized"
                          lightPalette:[[Palette alloc] initWithForegroundColor:@"#657b83"
                                                                backgroundColor:@"#fdf6e3"
                                                                    cursorColor:nil
                                                          colorPaletteOverrides:@[
                            @"#073642",
                            @"#dc322f",
                            @"#859900",
                            @"#b58900",
                            @"#268bd2",
                            @"#d33682",
                            @"#2aa198",
                            @"#eee8d5",
                            @"#002b36",
                            @"#cb4b16",
                            @"#586e75",
                            @"#657b83",
                            @"#839496",
                            @"#6c71c4",
                            @"#93a1a1",
                            @"#fdf6e3",
                          ]]
                           darkPalette:[[Palette alloc] initWithForegroundColor:@"#839496"
                                                                backgroundColor:@"#002b36"
                                                                    cursorColor:nil
                                                          colorPaletteOverrides:@[
                            @"#073642",
                            @"#dc322f",
                            @"#859900",
                            @"#b58900",
                            @"#268bd2",
                            @"#d33682",
                            @"#2aa198",
                            @"#eee8d5",
                            @"#002b36",
                            @"#cb4b16",
                            @"#586e75",
                            @"#657b83",
                            @"#839496",
                            @"#6c71c4",
                            @"#93a1a1",
                            @"#fdf6e3",
                           ]]
                            appearance:nil
            ],
            // Because this is a hidden theme, it needs to be last. There's
            // logic in UserPreferences and ThemesViewController which will not
            // work correctly otherwise.
            [[self alloc] initWithName:@"Hot Dog Stand"
                               palette:[[Palette alloc] initWithForegroundColor:@"#ff0"
                                                                backgroundColor:@"#f00"
                                                                    cursorColor:nil
                                                          colorPaletteOverrides:nil]
                            appearance:nil],
        ];
    }
    return defaultThemes;
}

+ (NSURL *)themesDirectory {
    return [[NSURL fileURLWithPath:NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject] URLByAppendingPathComponent:@"themes"];
}

+ (NSArray<Theme *> *)userThemes {
    NSMutableArray<Theme *> *themes = [NSMutableArray new];
    for (NSURL *file in [NSFileManager.defaultManager contentsOfDirectoryAtURL:self.themesDirectory includingPropertiesForKeys:nil options:0 error:nil]) {
        Theme *theme = [[Theme alloc] initWithName:file.lastPathComponent.stringByDeletingPathExtension data:[NSData dataWithContentsOfURL:file]];
        if (theme) {
            [themes addObject:theme];
        }
    }
    [themes sortUsingDescriptors:@[[NSSortDescriptor sortDescriptorWithKey:@"name" ascending:YES selector:@selector(localizedStandardCompare:)]]];
    return themes;
}

- (NSData *)data {
    NSMutableDictionary *representation = [@{
        @"version": @(THEME_VERSION),
    } mutableCopy];
    if (self.lightPalette == self.darkPalette) {
        representation[@"shared"] = self.lightPalette.serializedRepresentation;
    } else {
        representation[@"light"] = self.lightPalette.serializedRepresentation;
        representation[@"dark"] = self.darkPalette.serializedRepresentation;
    }
    if (self.appearance) {
        representation[@"appearance"] = self.appearance.serializedRepresentation;
    }
    return [NSJSONSerialization dataWithJSONObject:representation options:NSJSONWritingSortedKeys | NSJSONWritingPrettyPrinted error:nil];
}

+ (Theme *)themeForName:(NSString *)name includingDefaultThemes:(BOOL)includingDefaultThemes {
    // We should pick user themes over default ones, if they have the same name.
    NSMutableArray<Theme *> *themes = [Theme.userThemes mutableCopy];
    if (includingDefaultThemes) {
        [themes addObjectsFromArray:Theme.defaultThemes];
    }
    for (Theme *theme in themes) {
        if ([theme.name isEqualToString:name]) {
            return theme;
        }
    }
    return nil;
}

- (void)duplicateAsUserTheme {
    NSString *name;
    for (int suffix = 1; [self.class themeForName:name = [NSString stringWithFormat:@"%@-%d", self.name, suffix] includingDefaultThemes:NO]; ++suffix);
    [self.data writeToURL:[self.class.themesDirectory URLByAppendingPathComponent:[name stringByAppendingString:@".json"]] atomically:YES];
}

- (BOOL)addUserTheme {
    if ([self.class themeForName:self.name includingDefaultThemes:NO]) {
        return NO;
    } else {
        [self.data writeToURL:[self.class.themesDirectory URLByAppendingPathComponent:[self.name stringByAppendingString:@".json"]] atomically:YES];
        return YES;
    }
}

- (void)deleteUserTheme {
    [NSFileManager.defaultManager removeItemAtURL:[self.class.themesDirectory URLByAppendingPathComponent:[self.name stringByAppendingString:@".json"]] error:nil];
}

- (void)replaceWithUserTheme:(Theme *)theme {
    [theme.data writeToURL:[self.class.themesDirectory URLByAppendingPathComponent:[theme.name stringByAppendingString:@".json"]] atomically:YES];
    if (![self.name isEqualToString:theme.name]) {
        [self deleteUserTheme];
        [NSNotificationCenter.defaultCenter postNotificationName:ThemeUpdatedNotification object:theme.name];
    }
}
@end
