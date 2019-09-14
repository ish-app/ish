//
//  ViewController.h
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import <UIKit/UIKit.h>
#import "Terminal.h"
#import "TerminalViewControllerDelegate.h"

@interface TerminalViewController : UIViewController

@property (nonatomic) Terminal *terminal;
@property (nonatomic, weak) id <TerminalViewControllerDelegate> delegate;

- (void)switchTerminalToTTYNumber:(int)ttyNumber;

@end

extern struct tty_driver ios_tty_driver;

