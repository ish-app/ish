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
    [UserPreferences.shared observe:@[@"colorScheme"] options:NSKeyValueObservingOptionInitial
                              owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (@available(iOS 13, *)) {
                self.overrideUserInterfaceStyle = UserPreferences.shared.userInterfaceStyle;
            }
        });
    }];
}

@end
