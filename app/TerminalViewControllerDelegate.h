//
//  TerminalViewControllerDelegate.h
//  iSH
//
//  Created by Noah Peeters on 13.09.19.
//

#import <Foundation/Foundation.h>

@class TerminalViewController;

@protocol TerminalViewControllerDelegate <NSObject>

- (void) terminalViewController:(TerminalViewController *)terminalViewController requestedTTYWithNumber:(int)number;

@end
