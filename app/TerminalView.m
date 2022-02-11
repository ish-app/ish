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
#import "NSObject+SaneKVO.h"

struct rowcol {
    int row;
    int col;
};

struct API_AVAILABLE(ios(13.4)) functionKey {
    NSString *normalEscapeSequence;
    NSString *shiftEscapeSequence;
    NSString *controlEscapeSequence;
    UIKeyboardHIDUsage keyCode;
};

API_AVAILABLE(ios(13.4))
typedef struct functionKey functionKeyStruct;
NSTimer *keyRepeatTimer=nil;
NSTimer *keyStartTimer=nil;
BOOL initiateKeyRepeatTimer = FALSE;

// the following should be replaced by UserPreferences
const float keyRepeatStart = 0.3;
const float keyRepeatRepeat = 0.05;

@interface WeakScriptMessageHandler : NSObject <WKScriptMessageHandler>
@property (weak) id <WKScriptMessageHandler> handler;
@end
@implementation WeakScriptMessageHandler
- (instancetype)initWithHandler:(id <WKScriptMessageHandler>)handler {
    if (self = [super init]) {
        self.handler = handler;
    }
    return self;
}
- (void)userContentController:(WKUserContentController *)userContentController didReceiveScriptMessage:(WKScriptMessage *)message {
    [self.handler userContentController:userContentController didReceiveScriptMessage:message];
}
@end

@interface TerminalView ()

@property (nonatomic) NSMutableArray<UIKeyCommand *> *keyCommands;
@property (nonatomic) NSMutableArray *functionKeys;
@property ScrollbarView *scrollbarView;
@property (nonatomic) BOOL terminalFocused;

@property (nullable) NSString *markedText;
@property (nullable) NSString *selectedText;
@property UITextRange *markedRange;
@property UITextRange *selectedRange;

@property struct rowcol floatingCursor;
@property CGSize floatingCursorSensitivity;
@property CGSize actualFloatingCursorSensitivity;

@end

@implementation TerminalView
@synthesize inputDelegate;
@synthesize tokenizer;
@synthesize canBecomeFirstResponder;

- (void)awakeFromNib {
    [super awakeFromNib];
    self.inputAssistantItem.leadingBarButtonGroups = @[];
    self.inputAssistantItem.trailingBarButtonGroups = @[];

    ScrollbarView *scrollbarView = self.scrollbarView = [[ScrollbarView alloc] initWithFrame:self.bounds];
    scrollbarView.delegate = self;
    scrollbarView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    scrollbarView.bounces = NO;
    [self addSubview:scrollbarView];

    UserPreferences *prefs = UserPreferences.shared;
    [prefs observe:@[@"capsLockMapping", @"optionMapping", @"backtickMapEscape", @"overrideControlSpace"]
           options:0 owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_keyCommands = nil;
        });
    }];
    [prefs observe:@[@"fontFamily", @"fontSize", @"theme"]
           options:0 owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self _updateStyle];
        });
    }];

    self.markedRange = [UITextRange new];
    self.selectedRange = [UITextRange new];
}

- (void)dealloc {
    self.terminal = nil;
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    if (object == _terminal) {
        if (_terminal.loaded) {
            [self installTerminalView];
            [self _updateStyle];
        }
    }
}

static NSString *const HANDLERS[] = {@"syncFocus", @"focus", @"newScrollHeight", @"newScrollTop", @"openLink"};

- (void)setTerminal:(Terminal *)terminal {
    if (_terminal) {
        [_terminal removeObserver:self forKeyPath:@"loaded"];
        [self uninstallTerminalView];
    }

    _terminal = terminal;
    [_terminal addObserver:self forKeyPath:@"loaded" options:NSKeyValueObservingOptionInitial context:nil];
    if (_terminal.loaded)
        [self installTerminalView];
}

- (void)installTerminalView {
    NSAssert(_terminal.loaded, @"should probably not be installing a non-loaded terminal");
    UIView *superview = self.terminal.webView.superview;
    if (superview != nil) {
        NSAssert(superview == self.scrollbarView, @"installing terminal that is already installed elsewhere");
        return;
    }

    WKWebView *webView = _terminal.webView;
    _terminal.enableVoiceOverAnnounce = YES;
    webView.scrollView.scrollEnabled = NO;
    webView.scrollView.delaysContentTouches = NO;
    webView.scrollView.canCancelContentTouches = NO;
    webView.scrollView.panGestureRecognizer.enabled = NO;
    id <WKScriptMessageHandler> handler = [[WeakScriptMessageHandler alloc] initWithHandler:self];
    for (int i = 0; i < sizeof(HANDLERS)/sizeof(HANDLERS[0]); i++) {
        [webView.configuration.userContentController addScriptMessageHandler:handler name:HANDLERS[i]];
    }
    webView.frame = self.bounds;
    self.opaque = webView.opaque = NO;
    webView.backgroundColor = UIColor.clearColor;
    webView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

    self.scrollbarView.contentView = webView;
    [self.scrollbarView addSubview:webView];
}

- (void)uninstallTerminalView {
    // remove old terminal
    UIView *superview = _terminal.webView.superview;
    if (superview != self.scrollbarView) {
        NSAssert(superview == nil, @"uninstalling terminal that is installed elsewhere");
        return;
    }

    [_terminal.webView removeFromSuperview];
    self.scrollbarView.contentView = nil;
    for (int i = 0; i < sizeof(HANDLERS)/sizeof(HANDLERS[0]); i++) {
        [_terminal.webView.configuration.userContentController removeScriptMessageHandlerForName:HANDLERS[i]];
    }
    _terminal.enableVoiceOverAnnounce = NO;
}

#pragma mark Styling

- (NSString *)cssColor:(UIColor *)color {
    CGFloat red, green, blue, alpha;
    [color getRed:&red green:&green blue:&blue alpha:&alpha];
    return [NSString stringWithFormat:@"rgba(%ld, %ld, %ld, %ld)",
            lround(red * 255), lround(green * 255), lround(blue * 255), lround(alpha * 255)];
}

- (void)_updateStyle {
    NSAssert(NSThread.isMainThread, @"This method needs to be called on the main thread");
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
    [self.terminal.webView evaluateJavaScript:[NSString stringWithFormat:@"exports.updateStyle(%@)", json] completionHandler:^(id result, NSError *error){
        [self updateFloatingCursorSensitivity];
    }];
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

- (void)setTerminalFocused:(BOOL)terminalFocused {
    _terminalFocused = terminalFocused;
    NSString *script = terminalFocused ? @"exports.setFocused(true)" : @"exports.setFocused(false)";
    [self.terminal.webView evaluateJavaScript:script completionHandler:nil];
}

- (BOOL)becomeFirstResponder {
    self.terminalFocused = YES;
    [self reloadInputViews];
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

    if (self.controlKey.highlighted)
        self.controlKey.selected = YES;
    if (self.controlKey.selected) {
        if (!self.controlKey.highlighted)
            self.controlKey.selected = NO;
        if (text.length == 1)
            return [self insertControlChar:[text characterAtIndex:0]];
    }

    text = [text stringByReplacingOccurrencesOfString:@"\n" withString:@"\r"];
    NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
    [self.terminal sendInput:data];
}

// This method is used with text that requires no further processing; like the escape sequences from function keys
- (void)insertRawText:(NSString *)text {
    NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
    [self.terminal sendInput:data];
}

- (void)insertControlChar:(char)ch {
    if (strchr(controlKeys, ch) != NULL) {
        if (ch == ' ') ch = '\0';
        if (ch == '2') ch = '@';
        if (ch == '6') ch = '^';
        if (ch != '\0')
            ch = toupper(ch) ^ 0x40;
        [self.terminal sendInput:[NSData dataWithBytes:&ch length:1]];
    }
}


- (NSString *)setControlChar:(char)ch {
    if (strchr(controlKeys, ch) != NULL) {
        if (ch == ' ') ch = '\0';
        if (ch == '2') ch = '@';
        if (ch == '6') ch = '^';
        if (ch != '\0')
            ch = toupper(ch) ^ 0x40;
    } else {
        ch = '\0';
    }
    return [NSString stringWithFormat:@"%c", ch];
}



- (void)deleteBackward {
    [self insertText:@"\x7f"];
}

- (BOOL)hasText {
    return YES; // it's always ok to send a "delete"
}

#pragma mark IME Input and Selection

- (void)setMarkedText:(nullable NSString *)markedText selectedRange:(NSRange)selectedRange {
    self.markedText = markedText;
}

- (void)unmarkText {
    [self insertText:self.markedText];
}

- (UITextRange *)markedTextRange {
    if (self.markedText != nil)
        return self.markedRange;
    return nil;
}

// The only reason to have this selected range is to prevent the "speak selection" context action from failing to get the current selection and falling back on calling copy:. It doesn't even have to work, it seems...

- (UITextRange *)selectedTextRange {
    return self.selectedRange;
}

- (NSString *)textInRange:(UITextRange *)range {
    if (range == self.markedRange)
        return self.markedText;
    if (range == self.selectedRange)
        return @"";
    return nil;
}

- (id)insertDictationResultPlaceholder {
    return @"";
}
- (void)removeDictationResultPlaceholder:(id)placeholder willInsertResult:(BOOL)willInsertResult {
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

#pragma mark Floating cursor

- (void)updateFloatingCursorSensitivity {
    [self.terminal.webView evaluateJavaScript:@"exports.getCharacterSize()" completionHandler:^(NSArray *charSizeRaw, NSError *error) {
        if (error != nil) {
            NSLog(@"error getting character size: %@", error);
            return;
        }
        CGSize charSize = CGSizeMake([charSizeRaw[0] doubleValue], [charSizeRaw[1] doubleValue]);
        double sensitivity = 0.5;
        self.floatingCursorSensitivity = CGSizeMake(charSize.width / sensitivity, charSize.height / sensitivity);
    }];
}

- (struct rowcol)rowcolFromPoint:(CGPoint)point {
    CGSize sensitivity = self.actualFloatingCursorSensitivity;
    return (struct rowcol) {
        .row = (int) (-point.y / sensitivity.height),
        .col = (int) (point.x / sensitivity.width),
    };
}

- (void)beginFloatingCursorAtPoint:(CGPoint)point {
    self.actualFloatingCursorSensitivity = self.floatingCursorSensitivity;
    self.floatingCursor = [self rowcolFromPoint:point];
}

- (void)updateFloatingCursorAtPoint:(CGPoint)point {
    struct rowcol newPos = [self rowcolFromPoint:point];
    int rowDiff = newPos.row - self.floatingCursor.row;
    int colDiff = newPos.col - self.floatingCursor.col;
    NSMutableString *arrows = [NSMutableString string];
    for (int i = 0; i < abs(rowDiff); i++) {
        [arrows appendString:[self.terminal arrow:rowDiff > 0 ? 'A': 'B']];
    }
    for (int i = 0; i < abs(colDiff); i++) {
        [arrows appendString:[self.terminal arrow:colDiff > 0 ? 'C': 'D']];
    }
    [self insertText:arrows];
    self.floatingCursor = newPos;
}

- (void)endFloatingCursor {
    self.floatingCursor = (struct rowcol) {};
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
    /*
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
     */
}

static const char *alphabet = "abcdefghijklmnopqrstuvwxyz";
static const char *controlKeys = "abcdefghijklmnopqrstuvwxyz@^26-=[]\\ ";
static const char *metaKeys = "abcdefghijklmnopqrstuvwxyz0123456789-=[]\\;',./";

- (NSArray<UIKeyCommand *> *)keyCommands {
    if (_keyCommands != nil)
        return _keyCommands;
    _keyCommands = [NSMutableArray new];
/*
    [self addKeys:controlKeys withModifiers:UIKeyModifierControl];
    for (NSString *specialKey in @[UIKeyInputEscape, UIKeyInputUpArrow, UIKeyInputDownArrow,
                                   UIKeyInputLeftArrow, UIKeyInputRightArrow, @"\t"]) {
        [self addKey:specialKey withModifiers:0];
    }
*/
    if (UserPreferences.shared.capsLockMapping != CapsLockMapNone) {
        if (@available(iOS 13, *)); else {
            [self addKeys:controlKeys withModifiers:UIKeyModifierAlphaShift];
            [self addKeys:alphabet withModifiers:0];
            [self addKeys:alphabet withModifiers:UIKeyModifierShift];
            [self addKey:@"" withModifiers:UIKeyModifierAlphaShift]; // otherwise tap of caps lock can switch layouts
        }
    }
/*
    if (UserPreferences.shared.optionMapping == OptionMapEsc) {
        [self addKeys:metaKeys withModifiers:UIKeyModifierAlternate];
    }
    if (UserPreferences.shared.backtickMapEscape) {
        [self addKey:@"`" withModifiers:0];
    }
*/
    [_keyCommands addObject:[UIKeyCommand keyCommandWithInput:@"k"
                                                modifierFlags:UIKeyModifierCommand|UIKeyModifierShift
                                                       action:@selector(clearScrollback:)
                                         discoverabilityTitle:@"Clear Scrollback"]];

    // really should use something like NSDictionary instead of NSArray for better efficiency
    if (@available(iOS 13.4, *)) {
        _functionKeys = [[NSMutableArray alloc] init];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardUpArrow withNormalEscapeSequence:@"\x1b[A" withShiftEscapeSequence:@"\x1b[1;2A" withControlEscapeSequence:@"\x1b[1;5A"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardDownArrow withNormalEscapeSequence:@"\x1b[B" withShiftEscapeSequence:@"\x1b[1;2B" withControlEscapeSequence:@"\x1b[1;5B"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardRightArrow withNormalEscapeSequence:@"\x1b[C" withShiftEscapeSequence:@"\x1b[1;2C" withControlEscapeSequence:@"\x1b[1;5C"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardLeftArrow withNormalEscapeSequence:@"\x1b[D" withShiftEscapeSequence:@"\x1b[1;2D" withControlEscapeSequence:@"\x1b[1;5D"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardTab withNormalEscapeSequence:@"\t" withShiftEscapeSequence:@"\x1b[Z" withControlEscapeSequence:NULL];
        // escape sequence for ESC is empty because we prefix ESC with matching keys in this list
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardEscape withNormalEscapeSequence:@"\x1b" withShiftEscapeSequence:NULL withControlEscapeSequence:NULL];
        // Navigation keys
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardInsert withNormalEscapeSequence:@"\x1b[2~" withShiftEscapeSequence:@"\x1b[2;2~" withControlEscapeSequence:@"\x1b[2;5~"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardHelp withNormalEscapeSequence:@"\x1b[2~" withShiftEscapeSequence:@"\x1b[2;2~" withControlEscapeSequence:@"\x1b[2;5~"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardDeleteForward withNormalEscapeSequence:@"\x1b[3~" withShiftEscapeSequence:@"\x1b[3;2~" withControlEscapeSequence:@"\x1b[3;5~"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardPageUp withNormalEscapeSequence:@"\x1b[5~" withShiftEscapeSequence:@"\x1b[5;2~" withControlEscapeSequence:@"\x1b[5;5~"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardPageDown withNormalEscapeSequence:@"\x1b[6~" withShiftEscapeSequence:@"\x1b[6;2~" withControlEscapeSequence:@"\x1b[6;5~"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardHome withNormalEscapeSequence:@"\x1bOH" withShiftEscapeSequence:@"\x1b[1;2H" withControlEscapeSequence:@"\x1b[1;5H"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardEnd withNormalEscapeSequence:@"\x1bOF" withShiftEscapeSequence:@"\x1b[1;2F" withControlEscapeSequence:@"\x1b[1;5F"];
        // Function keys
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF1 withNormalEscapeSequence:@"\x1bOP" withShiftEscapeSequence:@"\x1b[1;2P" withControlEscapeSequence:@"\x1b[1;5P"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF2 withNormalEscapeSequence:@"\x1bOQ" withShiftEscapeSequence:@"\x1b[1;2Q" withControlEscapeSequence:@"\x1b[1;5Q"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF3 withNormalEscapeSequence:@"\x1bOR" withShiftEscapeSequence:@"\x1b[1;2R" withControlEscapeSequence:@"\x1b[1;5R"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF4 withNormalEscapeSequence:@"\x1bOS" withShiftEscapeSequence:@"\x1b[1;2S" withControlEscapeSequence:@"\x1b[1;5S"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF5 withNormalEscapeSequence:@"\x1b[15~" withShiftEscapeSequence:@"\x1b[15;2~" withControlEscapeSequence:@"\x1b[15;5~"];
        // Yes, @"\x1b[16~" is missing; it is meant to be
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF6 withNormalEscapeSequence:@"\x1b[17~" withShiftEscapeSequence:@"\x1b[17;2~" withControlEscapeSequence:@"\x1b[17;5~"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF7 withNormalEscapeSequence:@"\x1b[18~" withShiftEscapeSequence:@"\x1b[18;2~" withControlEscapeSequence:@"\x1b[18;5~"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF8 withNormalEscapeSequence:@"\x1b[19~" withShiftEscapeSequence:@"\x1b[19;2~" withControlEscapeSequence:@"\x1b[19;5~"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF9 withNormalEscapeSequence:@"\x1b[20~" withShiftEscapeSequence:@"\x1b[20;2~" withControlEscapeSequence:@"\x1b[20;5~"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF10 withNormalEscapeSequence:@"\x1b[21~" withShiftEscapeSequence:@"\x1b[21;2~" withControlEscapeSequence:@"\x1b[21;5~"];
        // Yes, @"\x1b[22~" is missing; it is meant to be
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF11 withNormalEscapeSequence:@"\x1b[23~" withShiftEscapeSequence:@"\x1b[23;2~" withControlEscapeSequence:@"\x1b[23;5~"];
        [self addFunctionKey:UIKeyboardHIDUsageKeyboardF12 withNormalEscapeSequence:@"\x1b[24~" withShiftEscapeSequence:@"\x1b[24;2~" withControlEscapeSequence:@"\x1b[24;5~"];

        // DEBUGGING
        functionKeyStruct fkey;
        for (NSValue *functionKey in _functionKeys ) {
            [functionKey getValue:&fkey];
            NSLog( @"keycode: %lx Normal: %@ Shift: %@ Control: %@",(long)fkey.keyCode, fkey.normalEscapeSequence,fkey.shiftEscapeSequence, fkey.controlEscapeSequence  );
        }
    }

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

- (void)addFunctionKey:(UIKeyboardHIDUsage)keyCode withNormalEscapeSequence:(NSString *)normalEscapeSequence withShiftEscapeSequence:(NSString *)shiftEscapeSequence withControlEscapeSequence:(NSString *)controlEscapeSequence API_AVAILABLE(ios(13.4)) {
    functionKeyStruct newkey;
    newkey.normalEscapeSequence = normalEscapeSequence;
    newkey.shiftEscapeSequence = shiftEscapeSequence;
    newkey.controlEscapeSequence = controlEscapeSequence;
    newkey.keyCode = keyCode;
    [_functionKeys addObject:[NSValue valueWithBytes:&newkey objCType:@encode(functionKeyStruct)]];
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

// This method runs once after a specified period before we start repeating the key
- (void)startKeyRepeatTimer:(NSTimer *)timer {
    NSLog(@"Got start repeat: %@ init %d", (NSString *)timer.userInfo, initiateKeyRepeatTimer);
    if ( initiateKeyRepeatTimer && keyRepeatTimer == nil) {
        keyRepeatTimer = [NSTimer scheduledTimerWithTimeInterval:keyRepeatRepeat target:self selector:@selector(insertRepeatText:) userInfo:timer.userInfo repeats:YES];
    }
}

// This method repeatedly called after key repeat period to insert the current key
- (void)insertRepeatText:(NSTimer *)timer {
    NSLog(@"Got repeat: %@ init %d", (NSString *)timer.userInfo, initiateKeyRepeatTimer);
    [self insertRawText:(NSString *)timer.userInfo];
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    bool handled = false;
    NSString *mykey;
    // reset any current key repeat timers
    initiateKeyRepeatTimer = FALSE;
    keyRepeatTimer = [self invalidateTimer:keyRepeatTimer];

    if (@available(iOS 13.4, *)) {
        UIKey *key;

        for (UIPress *aPress in presses) {
            key = aPress.key;
            handled = false;
            // Use of UIKeyboardHID was introduced in 13.4

            // ignore modifier keys by themselves
            if ( key.keyCode == UIKeyboardHIDUsageKeyboardLeftShift || key.keyCode == UIKeyboardHIDUsageKeyboardLeftControl || key.keyCode == UIKeyboardHIDUsageKeyboardLeftAlt || key.keyCode == UIKeyboardHIDUsageKeyboardRightShift || key.keyCode == UIKeyboardHIDUsageKeyboardRightControl || key.keyCode == UIKeyboardHIDUsageKeyboardRightAlt || key.keyCode == UIKeyboardHIDUsageKeyboardRightGUI ) {
                continue;
            }

            NSLog( @"Modified: %@ Unmodified: %@(%lu) init %d", key.characters, key.charactersIgnoringModifiers,(unsigned long)[key.charactersIgnoringModifiers length], initiateKeyRepeatTimer);

            functionKeyStruct fkey;
            // really should use something like NSDictionary instead of NSArray for better efficiency
            for (NSValue *functionKey in _functionKeys ) {
                [functionKey getValue:&fkey];
                if ( key.keyCode == fkey.keyCode ) {
                    UIKeyModifierFlags modifier = key.modifierFlags;
                    if ( modifier & UIKeyModifierAlphaShift) {
                        modifier &= ~UIKeyModifierAlphaShift;
                    }
                    if ( modifier == 0 && fkey.normalEscapeSequence )  {
                   //   mykey = [self.terminal escapeSequence:fkey.normalEscapeSequence];
                        mykey = fkey.normalEscapeSequence;
                        handled = true;
                        break;
                    }
                    if ( modifier & UIKeyModifierShift && fkey.shiftEscapeSequence )  {
//                      mykey = [self.terminal escapeSequence:fkey.shiftEscapeSequence];
                        mykey = fkey.shiftEscapeSequence;
                        handled = true;
                        break;
                    }
                    if ( modifier & UIKeyModifierControl && fkey.controlEscapeSequence )  {
//                      mykey = [self.terminal escapeSequence:fkey.controlEscapeSequence];
                        mykey = fkey.controlEscapeSequence;
                        handled = true;
                        break;
                    }
                }
            }
            if ( handled ) {
                [self insertRawText:mykey];
                initiateKeyRepeatTimer = TRUE;
                NSLog(@"Starting kyStartTimer...");
                keyStartTimer = [NSTimer scheduledTimerWithTimeInterval:keyRepeatStart target:self selector:@selector(startKeyRepeatTimer:) userInfo:mykey repeats:NO];
                continue;;
            }

            if ( [key.charactersIgnoringModifiers length] == 1 ) {
                // special check for TAB because we define it as a function key but it is only 1 character long in the unmodified function key definition
                /*
                if ( key.keyCode == UIKeyboardHIDUsageKeyboardDeleteForward ) {
                    // ensure this falls through to the Function Key check below
                } else if ( key.keyCode == UIKeyboardHIDUsageKeyboardHelp ) {
                    // ensure this falls through to the Function Key check below
                } else if (key.keyCode == UIKeyboardHIDUsageKeyboardTab) {
                   // for any modifiers set for TAB fall through to the check of function keys
                }
                else
                 */
                if ([key.charactersIgnoringModifiers isEqualToString:@"`"] && UserPreferences.shared.backtickMapEscape) {
                    mykey = @"\x1b";
//                    [self insertRawText:@"\x1b"];
                    handled = true;
                }
                else if (key.modifierFlags == 0) {
                    mykey = key.characters;
//                    [self insertRawText:key.characters];
                    handled = true;
                }
                else if (key.modifierFlags & UIKeyModifierShift) {
//                    [self insertRawText:[key.characters uppercaseString]];
                    mykey = [key.characters uppercaseString];
                    handled = true;
                }
                else if (key.modifierFlags & UIKeyModifierAlternate) {
                    mykey = [@"\x1b" stringByAppendingString:key.charactersIgnoringModifiers];
//                    [self insertRawText:[@"\x1b" stringByAppendingString:key.charactersIgnoringModifiers]];
                    handled = true;
                } else if (key.modifierFlags & UIKeyModifierAlphaShift) {
                // not sure if this is still required? ////[self handleCapsLockWithCommand:command];
                } else if (key.modifierFlags & UIKeyModifierControl) { // why was a check for AlphaShift included here? --> || command.modifierFlags & UIKeyModifierAlphaShift) {
                    /*
                    if ([key.charactersIgnoringModifiers isEqualToString:@"2"])
                        mykey = @"@";
                    else if ([key.charactersIgnoringModifiers isEqualToString:@"6"])
                        mykey = @"^";
                    else if ([key.charactersIgnoringModifiers isEqualToString:@"-"])
                        mykey = @"_";
                    else
                        mykey = key.charactersIgnoringModifiers;
                    [self insertControlChar:[mykey characterAtIndex:0]];
                    */
                    if ( key.keyCode == UIKeyboardHIDUsageKeyboardSpacebar ) {
                        if ( !UserPreferences.shared.overrideControlSpace ) {
                            continue;
                        }
                        mykey = [self setControlChar:' '];
                    } else {
                        mykey = [self setControlChar:[key.charactersIgnoringModifiers characterAtIndex:0]];
                    }
                    handled = true;
                }
                if ( handled ) {
                    [self insertRawText:mykey];
                    initiateKeyRepeatTimer = TRUE;
                    NSLog(@"***Setting init to true NOW");
                    keyStartTimer = [NSTimer scheduledTimerWithTimeInterval:keyRepeatStart target:self selector:@selector(startKeyRepeatTimer:) userInfo:mykey repeats:NO];
                    continue;
                }
            }
        }
    }
    if ( !handled) {
       return [super pressesBegan:presses withEvent:event];
    }
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
   // return [super pressesBegan:presses withEvent:event];
    keyRepeatTimer = [self invalidateTimer:keyRepeatTimer];
    keyStartTimer = [self invalidateTimer:keyStartTimer];
    initiateKeyRepeatTimer = FALSE;
}

- (NSTimer *)invalidateTimer:(NSTimer *) timer {
    NSLog(@"Key up: invalidate Timer %@ init %d",timer, initiateKeyRepeatTimer);
    if (timer != nil) {
        [timer invalidate];
        timer = nil;
    }
    return timer;
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
- (void)setSelectedTextRange:(UITextRange *)selectedTextRange { LogStub(); }
- (nonnull NSArray<UITextSelectionRect *> *)selectionRectsForRange:(nonnull UITextRange *)range { LogStub(); return @[]; }
- (nullable UITextRange *)textRangeFromPosition:(nonnull UITextPosition *)fromPosition toPosition:(nonnull UITextPosition *)toPosition { LogStub(); return nil; }

// conforming to UITextInput makes this view default to being an accessibility element, which blocks selecting anything in it
- (BOOL)isAccessibilityElement { return NO; }

@end
