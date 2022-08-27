//
//  Theme.h
//  iSH
//
//  Created by Saagar Jha on 2/25/22.
//

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface Palette : NSObject
@property(readonly) NSString *foregroundColor;
@property(readonly) NSString *backgroundColor;
@property(readonly, nullable) NSString *cursorColor;
@property(readonly, nullable) NSArray<NSString *> *colorPaletteOverrides;

- (instancetype)initWithForegroundColor:(nonnull NSString *)foregroundColor backgroundColor:(nonnull NSString *)backgroundColor cursorColor:(nullable NSString *)cursorColor colorPaletteOverrides:(nullable NSArray<NSString *> *)colorPaletteOverrides;
@end

@interface Theme : NSObject
@property(class, readonly) NSArray<Theme *> *defaultThemes;
@property(class, readonly) NSArray<Theme *> *userThemes;

@property(readonly) NSString *name;
@property(readonly) UIStatusBarStyle statusBarStyle;
@property(readonly) Palette *lightPalette;
@property(readonly) Palette *darkPalette;

+ (nullable Theme *)themeForName:(NSString *)name includingDefaultThemes:(BOOL)includingDefaultThemes;
- (instancetype)initWithName:(nonnull NSString *)name palette:(nonnull Palette *)palette;
- (instancetype)initWithName:(nonnull NSString *)name lightPalette:(nonnull Palette *)lightPalette darkPalette:(nonnull Palette *)darkPalette;
- (nullable instancetype)initWithName:(NSString *)name data:(NSData *)data;
- (void)duplicateAsUserTheme;
- (BOOL)addUserTheme;
- (void)deleteUserTheme;
- (void)replaceWithUserTheme:(Theme *)theme;
@end

@interface UIColor (iSH)
- (nullable instancetype)ish_initWithHexString:(NSString *)string;
@end

extern NSString *const ThemesUpdatedNotification;
extern NSString *const ThemeUpdatedNotification;

NS_ASSUME_NONNULL_END
