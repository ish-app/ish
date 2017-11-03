//
//  TerminalView.h
//  iSH
//
//  Created by Theodore Dubois on 11/3/17.
//

#import <UIKit/UIKit.h>
#import "Terminal.h"

@interface TerminalView : UIView <UIKeyInput>

@property (nonatomic) Terminal *terminal;

@end
