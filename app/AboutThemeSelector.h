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

struct CardPositions {
    CGPoint leftCenter;
    CGPoint middleCenter;
    CGPoint rightCenter;
};

@interface AboutThemeCard: UIView {
    UILabel *themeLabel;
    NSString *themeName;
}
- (void) setupAppearance;
- (void) updateAppearance;
- (id) initWithFrame:(CGRect)frame themeName:(NSString *)name;
@end

@interface AboutThemeSelector: UIView {
    UIView *leftCard;
    UIView *middleCard;
    UIView *rightCard;
    NSUInteger leftIdx;
    NSUInteger middleIdx;
    NSUInteger rightIdx;
    struct CardPositions cardPos;
    UserPreferences *prefs;
}

- (void) calculateCardPositions;
- (void) addStartingCards;

@end

NS_ASSUME_NONNULL_END
