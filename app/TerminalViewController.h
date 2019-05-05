//
//  ViewController.h
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import <UIKit/UIKit.h>
#import "Terminal.h"

@interface TerminalViewController : UIViewController

@property (nonatomic) Terminal *terminal;

@end

extern struct tty_driver ios_tty_driver;
