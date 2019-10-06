//
//  AboutNavigationController.m
//  iSH
//
//  Created by Theodore Dubois on 10/6/19.
//

#import "AboutNavigationController.h"
#import "UserPreferences.h"

@interface AboutNavigationController ()

@end

@implementation AboutNavigationController

- (void)viewDidLoad {
    [super viewDidLoad];
    [[UserPreferences shared] addObserver:self forKeyPath:@"theme" options:NSKeyValueObservingOptionNew|NSKeyValueObservingOptionInitial context:nil];
}

- (void)dealloc {
    @try {
        [[UserPreferences shared] removeObserver:self forKeyPath:@"theme"];
    } @catch (NSException * __unused exception) {}
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    if (@available(iOS 13, *)) {
        if ([keyPath isEqualToString:@"theme"]) {
            UIKeyboardAppearance appearance = UserPreferences.shared.theme.keyboardAppearance;
            if (appearance == UIKeyboardAppearanceDark) {
                self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
            } else {
                self.overrideUserInterfaceStyle = UIUserInterfaceStyleLight;
            }
        }
    }
}

@end
