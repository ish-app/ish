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
@property NSArray<UIKeyCommand *> *keyCommands;

@end

@implementation TerminalViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.terminal = [Terminal terminalWithType:0 number:0];
    [self.terminal.webView.configuration.preferences setValue:@YES forKey:@"developerExtrasEnabled"];
    TerminalView *termView = (TerminalView *) self.view;
    termView.terminal = self.terminal;
    [termView becomeFirstResponder];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(ishExited:)
                                                 name:ISHExitedNotification
                                               object:nil];
}

- (void)ishExited:(NSNotification *)notification {
    NSLog(@"exit");
}

- (void)keyPressed:(UIKeyCommand *)keyCommand {
    NSLog(@"keypress! %@", keyCommand.input);
}

@end
