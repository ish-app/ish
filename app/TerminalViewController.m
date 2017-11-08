//
//  ViewController.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import "TerminalViewController.h"
#import "AppDelegate.h"
#import "TerminalView.h"

@interface TerminalViewController () <UIGestureRecognizerDelegate>

@property Terminal *terminal;
@property UITapGestureRecognizer *tapRecognizer;
@property (weak, nonatomic) IBOutlet TerminalView *termView;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *bottomConstraint;

@end

@implementation TerminalViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.terminal = [Terminal terminalWithType:0 number:0];
    self.termView.terminal = self.terminal;
    [self.termView becomeFirstResponder];

    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserver:self
               selector:@selector(keyboardDidSomething:)
                   name:UIKeyboardDidShowNotification
                 object:nil];
    [center addObserver:self
               selector:@selector(keyboardDidSomething:)
                   name:UIKeyboardWillHideNotification
                 object:nil];

    [center addObserver:self
               selector:@selector(ishExited:)
                   name:ISHExitedNotification
                 object:nil];
}

- (void)keyboardDidSomething:(NSNotification *)notification {
    NSValue *frame = notification.userInfo[UIKeyboardFrameEndUserInfoKey];
    self.bottomConstraint.constant = -frame.CGRectValue.size.height;
    NSLog(@"bottom constant = %f", self.bottomConstraint.constant);
    [self.termView setNeedsUpdateConstraints];
}

- (void)ishExited:(NSNotification *)notification {
    NSLog(@"exit");
}

@end
