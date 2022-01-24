//
//  AboutNavigationController.m
//  iSH
//
//  Created by Theodore Dubois on 10/6/19.
//

#import "AboutNavigationController.h"
#import "UserPreferences.h"
#import "NSObject+SaneKVO.h"

@interface AboutNavigationController ()

@end

@implementation AboutNavigationController

- (void)viewDidLoad {
    [super viewDidLoad];
    [UserPreferences.shared observe:@[@"theme"] options:NSKeyValueObservingOptionInitial
                              owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (@available(iOS 13, *)) {
                UIKeyboardAppearance appearance = UserPreferences.shared.theme.keyboardAppearance;
                if (appearance == UIKeyboardAppearanceDark) {
                    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
                } else {
                    self.overrideUserInterfaceStyle = UIUserInterfaceStyleLight;
                }
            }
        });
    }];
}

@end
