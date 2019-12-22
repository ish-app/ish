//
//  TerminalView.m
//  iSH
//
//  Created by Theodore Dubois on 11/3/17.
//

#import "ScrollbarView.h"
#import "TerminalView.h"
#import "UserPreferences.h"
#import "UIApplication+OpenURL.h"

@interface TerminalView ()

@property (nonatomic) NSMutableArray<UIKeyCommand *> *keyCommands;
@property ScrollbarView *scrollbarView;
@property (nonatomic) BOOL terminalFocused;

@property (nullable) NSString *markedText;

@end

@implementation TerminalView
@synthesize inputDelegate;
@synthesize tokenizer;

static int kObserverMappings;
static int kObserverStyling;

- (void)awakeFromNib {
    [super awakeFromNib];
    self.inputAssistantItem.leadingBarButtonGroups = @[];
    self.inputAssistantItem.trailingBarButtonGroups = @[];

    ScrollbarView *scrollbarView = self.scrollbarView = [[ScrollbarView alloc] initWithFrame:self.bounds];
    self.scrollbarView = scrollbarView;
    scrollbarView.delegate = self;
    scrollbarView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    scrollbarView.bounces = NO;
    [self addSubview:scrollbarView];
    
    UserPreferences *prefs = UserPreferences.shared;
    [prefs addObserver:self forKeyPath:@"capsLockMapping" options:0 context:&kObserverMappings];
    [prefs addObserver:self forKeyPath:@"optionMapping" options:0 context:&kObserverMappings];
    [prefs addObserver:self forKeyPath:@"backtickMapEscape" options:0 context:&kObserverMappings];
    [prefs addObserver:self forKeyPath:@"fontFamily" options:0 context:&kObserverStyling];
    [prefs addObserver:self forKeyPath:@"fontSize" options:0 context:&kObserverStyling];
    [prefs addObserver:self forKeyPath:@"theme" options:0 context:&kObserverStyling];
}

- (void)dealloc {
    UserPreferences *prefs = UserPreferences.shared;
    [prefs removeObserver:self forKeyPath:@"capsLockMapping"];
    [prefs removeObserver:self forKeyPath:@"optionMapping"];
    [prefs removeObserver:self forKeyPath:@"backtickMapEscape"];
    [prefs removeObserver:self forKeyPath:@"fontFamily"];
    [prefs removeObserver:self forKeyPath:@"fontSize"];
    [prefs removeObserver:self forKeyPath:@"theme"];
    if (self.terminal)
        [self.terminal removeObserver:self forKeyPath:@"loaded"];
    self.terminal = nil;
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    if (object == self.terminal) {
        if (self.terminal.loaded) {
            [self _updateStyle];
        }
    } else if (context == &kObserverMappings) {
        _keyCommands = nil;
    } else if (context == &kObserverStyling) {
        [self _updateStyle];
    }
}

- (void)setTerminal:(Terminal *)terminal {
    NSArray<NSString *>* handlers = @[@"syncFocus", @"focus", @"newScrollHeight", @"newScrollTop", @"openLink"];
    
    if (self.terminal) {
        // remove old terminal
        NSAssert(self.terminal.webView.superview == self.scrollbarView, @"old terminal view was not in our view");
        [self.terminal.webView removeFromSuperview];
        self.scrollbarView.contentView = nil;
        for (NSString *handler in handlers) {
            [self.terminal.webView.configuration.userContentController removeScriptMessageHandlerForName:handler];
        }
        terminal.enableVoiceOverAnnounce = NO;
        [self.terminal removeObserver:self forKeyPath:@"loaded"];
    }
    
    _terminal = terminal;
    WKWebView *webView = terminal.webView;
    terminal.enableVoiceOverAnnounce = YES;
    webView.scrollView.scrollEnabled = NO;
    webView.scrollView.delaysContentTouches = NO;
    webView.scrollView.canCancelContentTouches = NO;
    webView.scrollView.panGestureRecognizer.enabled = NO;
    for (NSString *handler in handlers) {
        [webView.configuration.userContentController addScriptMessageHandler:self name:handler];
    }
    webView.frame = self.bounds;
    self.opaque = webView.opaque = NO;
    webView.backgroundColor = UIColor.clearColor;
    webView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.terminal addObserver:self forKeyPath:@"loaded" options:NSKeyValueObservingOptionInitial context:nil];
    
    self.scrollbarView.contentView = webView;
    [self.scrollbarView addSubview:webView];
}

#pragma mark Styling

- (NSString *)cssColor:(UIColor *)color {
    CGFloat red, green, blue, alpha;
    [color getRed:&red green:&green blue:&blue alpha:&alpha];
    return [NSString stringWithFormat:@"rgba(%ld, %ld, %ld, %ld)",
            lround(red * 255), lround(green * 255), lround(blue * 255), lround(alpha * 255)];
}

- (void)_updateStyle {
    if (!self.terminal.loaded)
        return;
    UserPreferences *prefs = [UserPreferences shared];
    if (_overrideFontSize == prefs.fontSize.doubleValue)
        _overrideFontSize = 0;
    id themeInfo = @{
        @"fontFamily": prefs.fontFamily,
        @"fontSize": @(self.effectiveFontSize),
        @"foregroundColor": [self cssColor:prefs.theme.foregroundColor],
        @"backgroundColor": [self cssColor:prefs.theme.backgroundColor],
    };
    NSString *json = [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:themeInfo options:0 error:nil] encoding:NSUTF8StringEncoding];
    [self.terminal.webView evaluateJavaScript:[NSString stringWithFormat:@"exports.updateStyle(%@)", json] completionHandler:nil];
}

- (void)setOverrideFontSize:(CGFloat)overrideFontSize {
    _overrideFontSize = overrideFontSize;
    [self _updateStyle];
}

- (CGFloat)effectiveFontSize {
    if (self.overrideFontSize != 0)
        return self.overrideFontSize;
    return UserPreferences.shared.fontSize.doubleValue;
}

#pragma mark Focus and scrolling

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (void)setTerminalFocused:(BOOL)terminalFocused {
    _terminalFocused = terminalFocused;
    NSString *script = terminalFocused ? @"exports.setFocused(true)" : @"exports.setFocused(false)";
    [self.terminal.webView evaluateJavaScript:script completionHandler:nil];
}

- (BOOL)becomeFirstResponder {
    self.terminalFocused = YES;
    return [super becomeFirstResponder];
}
- (BOOL)resignFirstResponder {
    self.terminalFocused = NO;
    return [super resignFirstResponder];
}
- (void)windowDidBecomeKey:(NSNotification *)notif {
    self.terminalFocused = YES;
}
- (void)windowDidResignKey:(NSNotification *)notif {
    self.terminalFocused = NO;
}

- (IBAction)loseFocus:(id)sender {
    [self resignFirstResponder];
}

- (void)willMoveToWindow:(UIWindow *)newWindow {
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    if (self.window != nil) {
        [center removeObserver:self
                          name:UIWindowDidBecomeKeyNotification
                        object:self.window];
        [center removeObserver:self
                          name:UIWindowDidResignKeyNotification
                        object:self.window];
    }
    if (newWindow != nil) {
        [center addObserver:self
                   selector:@selector(windowDidBecomeKey:)
                       name:UIWindowDidBecomeKeyNotification
                     object:newWindow];
        [center addObserver:self
                   selector:@selector(windowDidResignKey:)
                       name:UIWindowDidResignKeyNotification
                     object:newWindow];
    }
}

- (void)userContentController:(WKUserContentController *)userContentController didReceiveScriptMessage:(WKScriptMessage *)message {
    if ([message.name isEqualToString:@"syncFocus"]) {
        self.terminalFocused = self.terminalFocused;
    } else if ([message.name isEqualToString:@"focus"]) {
        if (!self.isFirstResponder) {
            [self becomeFirstResponder];
        }
    } else if ([message.name isEqualToString:@"newScrollHeight"]) {
        self.scrollbarView.contentSize = CGSizeMake(0, [message.body doubleValue]);
    } else if ([message.name isEqualToString:@"newScrollTop"]) {
        CGFloat newOffset = [message.body doubleValue];
        if (self.scrollbarView.contentOffset.y == newOffset)
            return;
        [self.scrollbarView setContentOffset:CGPointMake(0, newOffset) animated:NO];
    } else if ([message.name isEqualToString:@"openLink"]) {
        [UIApplication openURL:message.body];
    }
}

- (void)scrollViewDidScroll:(UIScrollView *)scrollView {
    [self.terminal.webView evaluateJavaScript:[NSString stringWithFormat:@"exports.newScrollTop(%f)", scrollView.contentOffset.y] completionHandler:nil];
}

- (void)setKeyboardAppearance:(UIKeyboardAppearance)keyboardAppearance {
    _keyboardAppearance = keyboardAppearance;
    if (keyboardAppearance == UIKeyboardAppearanceLight) {
        self.scrollbarView.indicatorStyle = UIScrollViewIndicatorStyleBlack;
    } else {
        self.scrollbarView.indicatorStyle = UIScrollViewIndicatorStyleWhite;
    }
}

#pragma mark Keyboard Input

// implementing these makes a keyboard pop up when this view is first responder

- (void)insertText:(NSString *)text {
    self.markedText = nil;
    if (self.controlKey.selected) {
        self.controlKey.selected = NO;
        if (text.length == 1)
            return [self insertControlChar:[text characterAtIndex:0]];
    }
    text = [text stringByReplacingOccurrencesOfString:@"\n" withString:@"\r"];
    NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
    [self.terminal sendInput:data.bytes length:data.length];
}

- (void)insertControlChar:(char)ch {
    if (strchr(controlKeys, ch) != NULL) {
        if (ch == '2') ch = '@';
        if (ch == '6') ch = '^';
        ch = toupper(ch) ^ 0x40;
        [self.terminal sendInput:&ch length:1];
    }
}

- (void)deleteBackward {
    [self insertText:@"\x7f"];
}

- (BOOL)hasText {
    return YES; // it's always ok to send a "delete"
}

#pragma mark IME Input

- (void)setMarkedText:(nullable NSString *)markedText selectedRange:(NSRange)selectedRange {
    self.markedText = markedText;
}

- (UITextRange *)markedTextRange {
    if (self.markedText == nil) {
        return nil;
    }
    return [UITextRange new];
}

- (NSString *)textInRange:(UITextRange *)range {
    return self.markedText;
}

- (void)unmarkText {
    [self insertText:self.markedText];
}


#pragma mark Keyboard Actions

- (void)paste:(id)sender {
    NSString *string = UIPasteboard.generalPasteboard.string;
    if (string) {
        [self insertText:string];
    }
}

- (void)copy:(id)sender {
    [self.terminal.webView evaluateJavaScript:@"exports.copy()" completionHandler:nil];
}

- (void)clearScrollback:(UIKeyCommand *)command {
    [self.terminal.webView evaluateJavaScript:@"exports.clearScrollback()" completionHandler:nil];
}

- (id)forwardingTargetForSelector:(SEL)selector {
    if ([NSStringFromSelector(selector) hasPrefix:@"_accessibility"])
        return self.terminal.webView;
    return nil;
}

- (BOOL)canPerformAction:(SEL)action withSender:(id)sender {
    if ([NSStringFromSelector(action) hasPrefix:@"_accessibility"] && [self.terminal.webView canPerformAction:action withSender:sender])
        return YES;
    return [super canPerformAction:action withSender:sender];
}

#pragma mark Keyboard Traits

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
        if ([key isEqualToString:@"`"] && UserPreferences.shared.backtickMapEscape)
            key = UIKeyInputEscape;
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
    } else if (command.modifierFlags & UIKeyModifierAlternate) {
        [self insertText:[@"\x1b" stringByAppendingString:key]];
    } else if (command.modifierFlags & UIKeyModifierAlphaShift) {
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
        [self insertControlChar:[key characterAtIndex:0]];
    }
}

static const char *alphabet = "abcdefghijklmnopqrstuvwxyz";
static const char *controlKeys = "abcdefghijklmnopqrstuvwxyz@^26-=[]\\";
static const char *metaKeys = "abcdefghijklmnopqrstuvwxyz0123456789-=[]\\;',./";

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
    if (UserPreferences.shared.optionMapping == OptionMapEsc) {
        [self addKeys:metaKeys withModifiers:UIKeyModifierAlternate];
    }
    if (UserPreferences.shared.backtickMapEscape) {
        [self addKey:@"`" withModifiers:0];
    }
    [_keyCommands addObject:[UIKeyCommand keyCommandWithInput:@"k"
                                                modifierFlags:UIKeyModifierCommand|UIKeyModifierShift
                                                       action:@selector(clearScrollback:)
                                         discoverabilityTitle:@"Clear Scrollback"]];
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

#pragma mark UITextInput stubs

#if 0
#define LogStub() NSLog(@"%s", __func__)
#else
#define LogStub()
#endif

- (NSWritingDirection)baseWritingDirectionForPosition:(nonnull UITextPosition *)position inDirection:(UITextStorageDirection)direction { LogStub(); return NSWritingDirectionLeftToRight; }
- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(nonnull UITextRange *)range { LogStub(); }
- (UITextPosition *)beginningOfDocument { LogStub(); return nil; }
- (CGRect)caretRectForPosition:(nonnull UITextPosition *)position { LogStub(); return CGRectZero; }
- (nullable UITextRange *)characterRangeAtPoint:(CGPoint)point { LogStub(); return nil; }
- (nullable UITextRange *)characterRangeByExtendingPosition:(nonnull UITextPosition *)position inDirection:(UITextLayoutDirection)direction { LogStub(); return nil; }
- (nullable UITextPosition *)closestPositionToPoint:(CGPoint)point { LogStub(); return nil; }
- (nullable UITextPosition *)closestPositionToPoint:(CGPoint)point withinRange:(nonnull UITextRange *)range { LogStub(); return nil; }
- (NSComparisonResult)comparePosition:(nonnull UITextPosition *)position toPosition:(nonnull UITextPosition *)other { LogStub(); return NSOrderedSame; }
- (UITextPosition *)endOfDocument { LogStub(); return nil; }
- (CGRect)firstRectForRange:(nonnull UITextRange *)range { LogStub(); return CGRectZero; }
- (NSDictionary<NSAttributedStringKey,id> *)markedTextStyle { LogStub(); return nil; }
- (void)setMarkedTextStyle:(NSDictionary<NSAttributedStringKey,id> *)markedTextStyle { LogStub(); }
- (NSInteger)offsetFromPosition:(nonnull UITextPosition *)from toPosition:(nonnull UITextPosition *)toPosition { LogStub(); return 0; }
- (nullable UITextPosition *)positionFromPosition:(nonnull UITextPosition *)position inDirection:(UITextLayoutDirection)direction offset:(NSInteger)offset { LogStub(); return nil; }
- (nullable UITextPosition *)positionFromPosition:(nonnull UITextPosition *)position offset:(NSInteger)offset { LogStub(); return nil; }
- (nullable UITextPosition *)positionWithinRange:(nonnull UITextRange *)range farthestInDirection:(UITextLayoutDirection)direction { LogStub(); return nil; }
- (void)replaceRange:(nonnull UITextRange *)range withText:(nonnull NSString *)text { LogStub(); }
- (UITextRange *)selectedTextRange { LogStub(); return nil; }
- (void)setSelectedTextRange:(UITextRange *)selectedTextRange { LogStub(); }
- (nonnull NSArray<UITextSelectionRect *> *)selectionRectsForRange:(nonnull UITextRange *)range { LogStub(); return @[]; }
- (nullable UITextRange *)textRangeFromPosition:(nonnull UITextPosition *)fromPosition toPosition:(nonnull UITextPosition *)toPosition { LogStub(); return nil; }

// conforming to UITextInput makes this view default to being an accessibility element, which blocks selecting anything in it
- (BOOL)isAccessibilityElement { return NO; }

@end
