//
//  DismissSegue.m
//  iSH
//
//  Created by Theodore Dubois on 9/23/18.
//

#import "DismissSegue.h"

@implementation DismissSegue

- (void)perform {
    [self.sourceViewController.presentingViewController dismissViewControllerAnimated:YES completion:nil];
}

@end
