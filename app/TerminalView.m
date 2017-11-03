//
//  TerminalView.m
//  iSH
//
//  Created by Theodore Dubois on 11/3/17.
//

#import "TerminalView.h"

@implementation TerminalView

- (void)setTerminal:(Terminal *)terminal {
    _terminal = terminal;
    UIView *webView = terminal.webView;
    webView.frame = self.frame;
    [self addSubview:webView];
    webView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    webView.translatesAutoresizingMaskIntoConstraints = YES;
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

// most of the logic here is about getting input
// implementing these makes a keyboard pop up when this view is first responder

- (void)insertText:(NSString *)text {
    NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
    [self.terminal sendInput:data.bytes length:data.length];
}

- (void)deleteBackward {
    [self insertText:@"\b"];
}

- (BOOL)hasText {
    return YES; // it's always ok to send a "delete"
}

- (UITextSmartDashesType)smartDashesType API_AVAILABLE(ios(11)) {
    return UITextSmartDashesTypeNo;
}
- (UITextSmartQuotesType)smartQuotesType API_AVAILABLE(ios(11)) {
    return UITextSmartQuotesTypeNo;
}
- (UITextSmartInsertDeleteType)smartInsertDeleteType API_AVAILABLE(ios(11)) {
    return UITextSmartInsertDeleteTypeNo;
}

/*
This code is the hacks that will be needed to remap caps lock to control. They're commented out for now.

- (void)keyPressed:(UIKeyCommand *)keyCommand {
    NSLog(@"%@", keyCommand.input);
}

- (NSArray<UIKeyCommand *> *)createKeyCommands {
    NSMutableArray<UIKeyCommand *> *keyCommands = [NSMutableArray new];
    [keyCommands addObjectsFromArray:[self keyCommandsForModifiers:0]];
    [keyCommands addObjectsFromArray:[self keyCommandsForModifiers:UIKeyModifierShift]];
    [keyCommands addObjectsFromArray:[self keyCommandsForModifiers:UIKeyModifierControl]];
    [keyCommands addObjectsFromArray:[self keyCommandsForModifiers:UIKeyModifierAlphaShift]];
    return keyCommands;
}

- (NSArray<UIKeyCommand *> *)keyCommandsForModifiers:(UIKeyModifierFlags)modifiers {
    NSMutableArray<NSString *> *keys = [NSMutableArray new];
    [self addKeys:"qwertyuiopasdfghjklzxcvbnm1234567890-=[]\\;',./` " toArray:keys];
    [keys addObject:UIKeyInputEscape];
    [keys addObject:UIKeyInputUpArrow];
    [keys addObject:UIKeyInputDownArrow];
    [keys addObject:UIKeyInputLeftArrow];
    [keys addObject:UIKeyInputRightArrow];
    
    NSMutableArray<UIKeyCommand *> *keyCommands = [NSMutableArray new];
    for (NSString *key in keys) {
        [keyCommands addObject:[UIKeyCommand keyCommandWithInput:key
                                                   modifierFlags:modifiers
                                                          action:@selector(keyPressed:)]];
    }
    return keyCommands;
}

- (void)addKeys:(const char *)chars toArray:(NSMutableArray<NSString *> *)keys {
    for (size_t i = 0; chars[i] != '\0'; i++)
        [keys addObject:[NSString stringWithFormat:@"%c", chars[i]]];
}
*/

@end
