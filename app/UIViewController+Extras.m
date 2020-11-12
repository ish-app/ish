//
//  UIViewController+Extras.m
//  iSH
//
//  Created by Theodore Dubois on 9/23/18.
//

#import "UIViewController+Extras.h"

@implementation UIViewController (Extras)

- (void)presentError:(NSError *)error title:(NSString *)title {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:title message:error.localizedDescription preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

@end
