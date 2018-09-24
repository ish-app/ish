//
//  BarViewController.m
//  iSH
//
//  Created by Theodore Dubois on 9/23/18.
//

#import "BarViewController.h"
#import "ArrowBarButton.h"

@interface BarViewController ()

@property (weak, nonatomic) IBOutlet UIStackView *bar;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barTop;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barBottom;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barLeading;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barTrailing;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barButtonWidth;
@property (weak, nonatomic) IBOutlet UIButton *hideKeyboardButton;

@end

@implementation BarViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad) {
        [self.bar removeArrangedSubview:self.hideKeyboardButton];
        [self.hideKeyboardButton removeFromSuperview];
    }
    if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone) {
        self.view.frame = CGRectMake(0, 0, 100, 48);
    } else {
        self.view.frame = CGRectMake(0, 0, 100, 55);
    }
}

- (void)viewWillLayoutSubviews {
    CGSize screen = UIScreen.mainScreen.bounds.size;
    CGSize bar = self.view.bounds.size;
    // set sizing parameters on bar
    // numbers stolen from iVim and modified somewhat
    if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone) {
        // phone
        [self setBarHorizontalPadding:6 verticalPadding:6 buttonWidth:32];
    } else if (bar.width == screen.width || bar.width == screen.height) {
        // full-screen ipad
        [self setBarHorizontalPadding:15 verticalPadding:8 buttonWidth:43];
    } else if (bar.width <= 320) {
        // slide over
        [self setBarHorizontalPadding:8 verticalPadding:8 buttonWidth:26];
    } else {
        // split view
        [self setBarHorizontalPadding:10 verticalPadding:8 buttonWidth:36];
    }
    [UIView performWithoutAnimation:^{
        [self.view layoutIfNeeded];
    }];
}

- (void)setBarHorizontalPadding:(CGFloat)horizontal verticalPadding:(CGFloat)vertical buttonWidth:(CGFloat)buttonWidth {
    self.barLeading.constant = self.barTrailing.constant = horizontal;
    self.barTop.constant = self.barBottom.constant = vertical;
    self.barButtonWidth.constant = buttonWidth;
}

- (IBAction)pressEscape:(id)sender {
    [self pressKey:@"\x1b"];
}
- (IBAction)pressTab:(id)sender {
    [self pressKey:@"\t"];
}
- (void)pressKey:(NSString *)key {
    [UIApplication.sharedApplication sendAction:@selector(insertText:) to:nil from:key forEvent:nil];
}

- (IBAction)pressControl:(id)sender {
    self.controlKey.selected = !self.controlKey.selected;
}

- (IBAction)pressArrow:(ArrowBarButton *)sender {
    switch (sender.direction) {
        case ArrowUp: [self pressKey:@"\x1b[A"]; break;
        case ArrowDown: [self pressKey:@"\x1b[B"]; break;
        case ArrowLeft: [self pressKey:@"\x1b[D"]; break;
        case ArrowRight: [self pressKey:@"\x1b[C"]; break;
        case ArrowNone: break;
    }
}

@end

@interface BarView : UIView
@property IBOutlet UIViewController *barViewController;
@end
@implementation BarView

- (void)layoutSubviews {
    // this is broken for some reason otherwise
    [self.barViewController viewWillLayoutSubviews];
    [super layoutSubviews];
}

@end
