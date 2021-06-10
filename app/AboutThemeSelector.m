//
//  AboutThemeSelector.m
//  iSH
//
//  Created by Corban Amouzou on 2021-06-08.
//

#import "AboutThemeSelector.h"
#import "UserPreferences.h"
#import "NSObject+SaneKVO.h"
#import "UIColor+isLight.h"
#import "CALayer+ShadowEnhancement.h"


@implementation AboutThemeSelector

- (id) initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    prefs = UserPreferences.shared;
    
    NSArray<NSString *> *themeNames = Theme.themeNames;

    NSUInteger maxIdx = themeNames.count;
    CGFloat verticalPos = cardVerticalPadding;
    for (int i = 0; i < maxIdx; i++) {
        UITapGestureRecognizer *handleTap = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(updateTheme:)];
        AboutThemeCard * currentCard = [[AboutThemeCard alloc] initWithFrame:CGRectMake(0, verticalPos, self.frame.size.width, cardSize)
                                                                   themeName:themeNames[i]];
        [currentCard addGestureRecognizer:handleTap];
        verticalPos = verticalPos + cardSize + cardVerticalPadding;
        [cards addObject: currentCard];
        [self addSubview:currentCard];
    }
    return self;
}

- (void) updateTheme:(UITapGestureRecognizer *)recognizer {
    AboutThemeCard *card =  (AboutThemeCard *) recognizer.view;
    [UserPreferences.shared setThemeTo:card.themeName];
}
@end

@implementation AboutThemeCard
- (id) initWithFrame:(CGRect)frame themeName:(NSString *)name {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _themeName = name;
    [UserPreferences.shared observe:@[@"theme", @"fontSize", @"fontFamily"]
                            options:0 owner:self usingBlock:^(typeof(self) self) {
        [self updateAppearance];
    }];
    [self setupAppearance];
    return self;
}
- (void) setupAppearance {
    UserPreferences *prefs = [UserPreferences shared];
    Theme *currentTheme = [prefs themeFromName:_themeName];
    
    UIImage *checkmarkImage = [UIImage imageNamed:@"checkmark"];
    checkmarkImage = [checkmarkImage imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];
    checkMark = [[UIImageView alloc] initWithImage:checkmarkImage];
    checkMark.frame = CGRectMake(self.frame.size.width - 50 ,(self.frame.size.height / 2) - (25 / 2), 25, 25);
    if (!currentTheme.backgroundColor.isLight) {
        [checkMark setTintColor:UIColor.whiteColor];
    } else {
        [checkMark setTintColor:UIColor.blackColor];
    }
    themeLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 30, 30)];
    [self addSubview:themeLabel];
    themeLabel.text = [NSString stringWithFormat:@"%@:~# %@", UIDevice.currentDevice.name, currentTheme.name];
    self.backgroundColor = currentTheme.backgroundColor;
    [self updateAppearance];
}

- (void) updateAppearance {
    __block Theme *currentTheme = [UserPreferences.shared themeFromName:_themeName];
    themeLabel.font = [UIFont fontWithName:UserPreferences.shared.fontFamily size:UserPreferences.shared.fontSize.doubleValue];
    
    [themeLabel sizeToFit];
    [themeLabel setCenter:CGPointMake(themeLabel.frame.size.width/2 + 20, self.frame.size.height / 2)];
    themeLabel.textColor = currentTheme.backgroundColor;
    [UIView animateWithDuration:0.4  animations:^{
        self->themeLabel.textColor = currentTheme.foregroundColor;
        [self.layer setAdvancedShadowWithColor:UIColor.blackColor alpha:0.4 x:0 y:3 blur:5 spread:0];
        if (self.themeName == UserPreferences.shared.theme.name && !([self.subviews containsObject:self->checkMark])) {
            self->checkMark.alpha = 0.0;
            [self addSubview:self->checkMark];
            self->checkMark.alpha = 1.0;
        } else {
            [self willRemoveSubview:self->checkMark];
            self->checkMark.alpha = 0.0;
            [self->checkMark removeFromSuperview];
        }
    }];
}

@end
