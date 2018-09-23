//
//  TerminalView.h
//  iSH
//
//  Created by Theodore Dubois on 11/3/17.
//

#import <UIKit/UIKit.h>
#import "Terminal.h"

@interface TerminalView : UIView <UIKeyInput, WKScriptMessageHandler>

@property UIInputView *inputAccessoryView;

@property (nonatomic) Terminal *terminal;
@property (readonly) UIScrollView *scrollView;

@property (weak) UIButton *controlKey;

@end
