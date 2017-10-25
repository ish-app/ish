//
//  ViewController.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import "TerminalViewController.h"

@interface TerminalViewController ()

@property Terminal *terminal;
@property (weak, nonatomic) IBOutlet UITextView *textView;

@end

@implementation TerminalViewController

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey,id> *)change
                       context:(void *)context {
    self.textView.text = [self.textView.text stringByAppendingString:self.terminal.content];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.terminal = [Terminal terminalWithType:0 number:0];
    [self.terminal addObserver:self
                    forKeyPath:@"content"
                       options:NSKeyValueObservingOptionInitial
                       context:NULL];
}


- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

@end

