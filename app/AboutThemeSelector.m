//
//  AboutThemeSelector.m
//  iSH
//
//  Created by Corban Amouzou on 2021-06-08.
//

#import "AboutThemeSelector.h"
#import "UserPreferences.h"
#import "NSObject+SaneKVO.h"
#import "CALayer+ShadowEnhancement.h"

static CGFloat cardSize = 160;
static CGFloat cardUncenterSize = 100;

@implementation AboutThemeSelector

- (id) initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    prefs = UserPreferences.shared;
    
    NSArray<NSString *> *themeNames = Theme.themeNames;
    [self calculateCardPositions];
    NSUInteger maxIdx = themeNames.count - 1;
    for (int i = 0; i < maxIdx; i++) {
        // This should only run once
        if (prefs.theme.name == themeNames[i]) {
            middleIdx = i;
            
            if ((i - 1) < 0) leftIdx = maxIdx;
            else leftIdx = i - 1;
            
            if ((i + 1) > maxIdx) rightIdx = 0;
            else rightIdx = i + 1;
        }
    }
    [self addStartingCards];
    return self;
}

- (void) calculateCardPositions {
    CGFloat parentWidth = self.frame.size.width;
    CGFloat parentHeight = self.frame.size.height;
    cardPos.leftCenter = CGPointMake(0 - (cardUncenterSize / 2), parentHeight/2 - (cardUncenterSize / 2));
    cardPos.middleCenter = CGPointMake(parentWidth/2 - (cardSize / 2), parentHeight/2 - (cardSize / 2));
    cardPos.rightCenter = CGPointMake(parentWidth - (cardUncenterSize / 2), parentHeight/2 - (cardUncenterSize / 2));
}

- (void) addStartingCards {
    // Left Card
    leftCard = [[AboutThemeCard alloc] initWithFrame:CGRectMake(cardPos.leftCenter.x, cardPos.leftCenter.y, cardUncenterSize, cardUncenterSize)
                                           themeName:Theme.themeNames[leftIdx]];
    // Middle Card
    middleCard = [[AboutThemeCard alloc] initWithFrame:CGRectMake(cardPos.middleCenter.x, cardPos.middleCenter.y, cardSize, cardSize)
                                           themeName:Theme.themeNames[middleIdx]];
    // Right Card
    rightCard = [[AboutThemeCard alloc] initWithFrame:CGRectMake(cardPos.rightCenter.x, cardPos.rightCenter.y, cardUncenterSize, cardUncenterSize)
                                           themeName:Theme.themeNames[rightIdx]];
    // Add to View
    [self addSubview:leftCard];
    [self addSubview:middleCard];
    [self addSubview:rightCard];
}
@end

@implementation AboutThemeCard
- (id) initWithFrame:(CGRect)frame themeName:(NSString *)name {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    themeName = name;
    [UserPreferences.shared observe:@[@"theme", @"fontSize", @"fontFamily"]
                            options:0 owner:self usingBlock:^(typeof(self) self) {
        [self updateAppearance];
    }];
    [self setupAppearance];
    return self;
}
- (void) setupAppearance {
    UserPreferences *prefs = [UserPreferences shared];
    Theme *currentTheme = [prefs themeFromName:themeName];
    
    // Label
    themeLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 30, 30)]; // Size doesn't matter all that much ;)
    [self addSubview:themeLabel];
    themeLabel.text = [@"~# " stringByAppendingString:currentTheme.name];
    themeLabel.font = [UIFont fontWithName:prefs.fontFamily size:prefs.fontSize.doubleValue];
    themeLabel.textColor = currentTheme.foregroundColor;
    [themeLabel sizeToFit]; // Cuz this
    [themeLabel setCenter:CGPointMake(self.frame.size.width / 2, self.frame.size.height / 2)];
    // Self
    self.backgroundColor = currentTheme.backgroundColor;
    [self.layer setAdvancedShadowWithColor:[UIColor blackColor] alpha:0.3f x:0 y:4 blur:13 spread:0];
    self.layer.cornerRadius = 20;
}

- (void) updateAppearance {
    themeLabel.font = [UIFont fontWithName:UserPreferences.shared.fontFamily size:UserPreferences.shared.fontSize.doubleValue];
    [themeLabel sizeToFit];
    [themeLabel setCenter:CGPointMake(self.frame.size.width / 2, self.frame.size.height / 2)];
}

@end
