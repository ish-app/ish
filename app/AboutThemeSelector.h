//
//  AboutThemeSelector.h
//  iSH
//
//  Created by Corban Amouzou on 2021-06-08.
//
#import <Foundation/Foundation.h>
#import "UserPreferences.h"
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

static CGFloat cardSize = 45;
static CGFloat cardHorizontalPadding = 15;
static CGFloat cardVerticalPadding = 15;

@interface AboutThemeCard: UIView {
    UILabel *themeLabel;
    UIImageView *checkMark;
}
@property (nonatomic) NSString *themeName;
- (void) setupAppearance;
- (void) updateAppearance;
- (id) initWithFrame:(CGRect)frame themeName:(NSString *)name;
@end

@interface AboutThemeSelector: UIView {
    NSMutableArray<AboutThemeCard *> *cards;
    UserPreferences *prefs;
}

@end

NS_ASSUME_NONNULL_END
