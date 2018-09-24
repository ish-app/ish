//
//  ViewController.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import "TerminalViewController.h"
#import "AppDelegate.h"
#import "TerminalView.h"
#import "BarViewController.h"

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
    BarViewController *barViewController = [self.storyboard instantiateViewControllerWithIdentifier:@"bar"];
    UIView *barView = barViewController.view;
    UIInputView *accessoryView = [[UIInputView alloc] initWithFrame:barView.frame inputViewStyle:UIInputViewStyleDefault];
    [accessoryView addSubview:barView];
    self.termView.inputAccessoryView = accessoryView;
    self.termView.controlKey = barViewController.controlKey; // help me
    [self.termView becomeFirstResponder];

    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserver:self
               selector:@selector(keyboardDidSomething:)
                   name:UIKeyboardWillShowNotification
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

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (void)keyboardDidSomething:(NSNotification *)notification {
    BOOL initialLayout = self.termView.needsUpdateConstraints;
    
    CGFloat pad = 0;
    if ([notification.name isEqualToString:UIKeyboardWillShowNotification]) {
        NSValue *frame = notification.userInfo[UIKeyboardFrameEndUserInfoKey];
        pad = frame.CGRectValue.size.height;
    }
    self.bottomConstraint.constant = -pad;
    [self.view setNeedsUpdateConstraints];
    
    if (!initialLayout) {
        // if initial layout hasn't happened yet, the terminal view is going to be at a really weird place, so animating it is going to look really bad
        NSNumber *interval = notification.userInfo[UIKeyboardAnimationDurationUserInfoKey];
        NSNumber *curve = notification.userInfo[UIKeyboardAnimationCurveUserInfoKey];
        [UIView animateWithDuration:interval.doubleValue
                              delay:0
                            options:curve.integerValue << 16
                         animations:^{
                             [self.view layoutIfNeeded];
                         }
                         completion:nil];
    }
}

- (void)ishExited:(NSNotification *)notification {
    [self performSelectorOnMainThread:@selector(displayExitThing) withObject:nil waitUntilDone:YES];
}

- (void)displayExitThing {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"attempted to kill init" message:nil preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"goodbye" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        exit(0);
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (IBAction)showAbout:(id)sender {
    UIViewController *aboutViewController = [[UIStoryboard storyboardWithName:@"About" bundle:nil] instantiateInitialViewController];
    [self presentViewController:aboutViewController animated:YES completion:nil];
}

@end
