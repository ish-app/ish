//
//  TerminalView.m
//  iSH
//
//  Created by Theodore Dubois on 11/3/17.
//

#import "TerminalView.h"
#import "UserPreferences.h"

@interface TerminalView ()

@property (nonatomic) NSMutableArray<UIKeyCommand *> *keyCommands;

@end

@implementation TerminalView

- (void)setTerminal:(Terminal *)terminal {
    if (self.terminal) {
        // remove old terminal
        NSAssert(self.terminal.webView.superview == self, @"old terminal view was not in our view");
        [self.terminal.webView removeFromSuperview];
        [self.terminal.webView.configuration.userContentController removeScriptMessageHandlerForName:@"focus"];
    }
    
    _terminal = terminal;
    WKWebView *webView = terminal.webView;
    webView.scrollView.scrollEnabled = NO;
    webView.scrollView.delaysContentTouches = NO;
    webView.scrollView.canCancelContentTouches = NO;
    webView.scrollView.panGestureRecognizer.enabled = NO;
    [webView.configuration.userContentController addScriptMessageHandler:self name:@"focus"];
    webView.frame = self.frame;
    self.opaque = webView.opaque = NO;
    webView.backgroundColor = UIColor.clearColor;
    [self addSubview:webView];
    webView.translatesAutoresizingMaskIntoConstraints = NO;
    [webView.topAnchor constraintEqualToAnchor:self.topAnchor].active = YES;
    [webView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor].active = YES;
    [webView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor].active = YES;
    [webView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor].active = YES;
}

- (UIScrollView *)scrollView {
    return self.terminal.webView.scrollView;
}

#pragma mark Focus

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (void)awakeFromNib {
    [super awakeFromNib];
    self.inputAssistantItem.leadingBarButtonGroups = @[];
    self.inputAssistantItem.trailingBarButtonGroups = @[];
}

- (void)userContentController:(WKUserContentController *)userContentController didReceiveScriptMessage:(WKScriptMessage *)message {
    if ([message.body isEqualToString:@"focus"]) {
        if (!self.isFirstResponder) {
            [self becomeFirstResponder];
        }
    }
}

- (BOOL)resignFirstResponder {
    [self.terminal.webView evaluateJavaScript:@"term.blur()" completionHandler:nil];
    return [super resignFirstResponder];
}

- (IBAction)loseFocus:(id)sender {
    [self resignFirstResponder];
}

// implementing these makes a keyboard pop up when this view is first responder

#pragma mark Keyboard

- (void)insertText:(NSString *)text {
    if (self.controlKey.selected) {
        self.controlKey.selected = NO;
        if (text.length == 1) {
            char ch = [text characterAtIndex:0];
            if (strchr(controlKeys, ch) != NULL) {
                ch = toupper(ch) ^ 0x40;
                text = [NSString stringWithFormat:@"%c", ch];
            }
        }
    }
    text = [text stringByReplacingOccurrencesOfString:@"\n" withString:@"\r"];
    NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
    [self.terminal sendInput:data.bytes length:data.length];
}

- (BOOL)canPerformAction:(SEL)action withSender:(id)sender {
    if ([NSStringFromSelector(action) hasPrefix:@"_accessibility"] && [self.terminal.webView canPerformAction:action withSender:sender])
        return YES;
    return [super canPerformAction:action withSender:sender];
}

- (void)paste:(id)sender {
    NSString *string = UIPasteboard.generalPasteboard.string;
    if (string) {
        [self insertText:string];
    }
}

- (void)copy:(id)sender {
    [self.terminal.webView evaluateJavaScript:@"window.getSelection().toString()" completionHandler:^(NSString *selection, NSError *err) {
        if (err != nil) {
            NSLog(@"error copying: %@", err);
            return;
        }
        // xterm uses nonbreaking spaces for no apparent reason
        selection = [selection stringByReplacingOccurrencesOfString:@"\u00a0" withString:@" "];
        UIPasteboard.generalPasteboard.string = selection;
    }];
}

- (id)forwardingTargetForSelector:(SEL)selector {
    if ([NSStringFromSelector(selector) hasPrefix:@"_accessibility"])
        return self.terminal.webView;
    return nil;
}

- (void)deleteBackward {
    [self insertText:@"\x7f"];
}

- (BOOL)hasText {
    return YES; // it's always ok to send a "delete"
}

- (void)clearScreen:(UIKeyCommand *)command {
    [self.terminal.webView evaluateJavaScript:@"term.clear()" completionHandler:nil];
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
- (UITextAutocapitalizationType)autocapitalizationType {
    return UITextAutocapitalizationTypeNone;
}
- (UITextAutocorrectionType)autocorrectionType {
    return UITextAutocorrectionTypeNo;
}

#pragma mark Hardware Keyboard

- (void)handleKeyCommand:(UIKeyCommand *)command {
    NSString *key = command.input;
    if (command.modifierFlags == 0) {
        if ([key isEqualToString:UIKeyInputEscape])
            key = @"\x1b";
        else if ([key isEqualToString:UIKeyInputUpArrow])
            key = [self.terminal arrow:'A'];
        else if ([key isEqualToString:UIKeyInputDownArrow])
            key = [self.terminal arrow:'B'];
        else if ([key isEqualToString:UIKeyInputLeftArrow])
            key = [self.terminal arrow:'D'];
        else if ([key isEqualToString:UIKeyInputRightArrow])
            key = [self.terminal arrow:'C'];
        [self insertText:key];
    } else if (command.modifierFlags & UIKeyModifierShift) {
        [self insertText:[key uppercaseString]];
    } else if(command.modifierFlags & UIKeyModifierAlphaShift) {
        [self handleCapsLockWithCommand:command];
    } else if (command.modifierFlags & UIKeyModifierControl || command.modifierFlags & UIKeyModifierAlphaShift) {
        if (key.length == 0)
            return;
        if ([key isEqualToString:@"2"])
            key = @"@";
        else if ([key isEqualToString:@"6"])
            key = @"^";
        else if ([key isEqualToString:@"-"])
            key = @"_";
        char ch = (char) [key.uppercaseString characterAtIndex:0] ^ 0x40;
        [self insertText:[NSString stringWithFormat:@"%c", ch]];
    }
}

static const char *alphabet = "abcdefghijklmnopqrstuvwxyz";
static const char *controlKeys = "abcdefghijklmnopqrstuvwxyz26-=[]\\";

- (NSArray<UIKeyCommand *> *)keyCommands {
    if (_keyCommands != nil)
        return _keyCommands;
    _keyCommands = [NSMutableArray new];
    [self addKeys:controlKeys withModifiers:UIKeyModifierControl];
    for (NSString *specialKey in @[UIKeyInputEscape, UIKeyInputUpArrow, UIKeyInputDownArrow,
                                   UIKeyInputLeftArrow, UIKeyInputRightArrow, @"\t"]) {
        [self addKey:specialKey withModifiers:0];
    }
    if (UserPreferences.shared.capsLockMapping != CapsLockMapNone) {
        [self addKeys:controlKeys withModifiers:UIKeyModifierAlphaShift];
        [self addKeys:alphabet withModifiers:0];
        [self addKeys:alphabet withModifiers:UIKeyModifierShift];
        [self addKey:@"" withModifiers:UIKeyModifierAlphaShift]; // otherwise tap of caps lock can switch layouts
    }
    [_keyCommands addObject:[UIKeyCommand keyCommandWithInput:@"k"
                                                modifierFlags:UIKeyModifierCommand
                                                       action:@selector(clearScreen:)
                                         discoverabilityTitle:@"Clear Screen"]];
    return _keyCommands;
}

- (void)addKeys:(const char *)keys withModifiers:(UIKeyModifierFlags)modifiers {
    for (size_t i = 0; keys[i] != '\0'; i++) {
        [self addKey:[NSString stringWithFormat:@"%c", keys[i]] withModifiers:modifiers];
    }
}

- (void)addKey:(NSString *)key withModifiers:(UIKeyModifierFlags)modifiers {
    UIKeyCommand *command = [UIKeyCommand keyCommandWithInput:key
                                                modifierFlags:modifiers
                                                       action:@selector(handleKeyCommand:)];
    [_keyCommands addObject:command];
    
}

- (void)keyCommandTriggered:(UIKeyCommand *)sender {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self handleKeyCommand:sender];
    });
}

- (void)handleCapsLockWithCommand:(UIKeyCommand *)command {
    CapsLockMapping target = UserPreferences.shared.capsLockMapping;
    NSString *newInput = command.input ? command.input : @"";
    UIKeyModifierFlags flags = command.modifierFlags;
    flags ^= UIKeyModifierAlphaShift;
    if(target == CapsLockMapEscape) {
        newInput = UIKeyInputEscape;
    } else if(target == CapsLockMapControl) {
        if([newInput length] == 0) {
            return;
        }
        flags |= UIKeyModifierControl;
    } else {
        return;
    }

    UIKeyCommand *newCommand = [UIKeyCommand keyCommandWithInput:newInput
                                                   modifierFlags:flags
                                                          action:@selector(keyCommandTriggered:)];
    [self handleKeyCommand:newCommand];
}

@end
