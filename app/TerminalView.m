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
    [prefs observe:@[@"colorScheme", @"fontFamily", @"fontSize", @"theme", @"cursorStyle", @"blinkCursor"]
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

- (void)_updateStyle {
    NSAssert(NSThread.isMainThread, @"This method needs to be called on the main thread");
    if (!self.terminal.loaded)
        return;
    UserPreferences *prefs = [UserPreferences shared];
    if (_overrideFontSize == prefs.fontSize.doubleValue)
        _overrideFontSize = 0;
    Palette *palette = prefs.palette;
    if (self.overrideAppearance != OverrideAppearanceNone) {
        palette = self.overrideAppearance == OverrideAppearanceLight ? prefs.theme.lightPalette : prefs.theme.darkPalette;
    }
    NSMutableDictionary<NSString *, id> *themeInfo = [@{
        @"fontFamily": prefs.fontFamily,
        @"fontSize": @(self.effectiveFontSize),
        @"foregroundColor": palette.foregroundColor,
        @"backgroundColor": palette.backgroundColor,
        @"blinkCursor": @(prefs.blinkCursor),
        @"cursorShape": prefs.htermCursorShape,
    } mutableCopy];
    if (prefs.palette.colorPaletteOverrides) {
        themeInfo[@"colorPaletteOverrides"] = palette.colorPaletteOverrides;
    }
    NSString *json = [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:themeInfo options:0 error:nil] encoding:NSUTF8StringEncoding];
    [self.terminal.webView evaluateJavaScript:[NSString stringWithFormat:@"exports.updateStyle(%@)", json] completionHandler:^(id result, NSError *error){
        [self updateFloatingCursorSensitivity];
    }];
}

- (void)setOverrideFontSize:(CGFloat)overrideFontSize {
    _overrideFontSize = overrideFontSize;
    [self _updateStyle];
}

- (void)setOverrideAppearance:(enum OverrideAppearance)overrideAppearance {
    _overrideAppearance = overrideAppearance;
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
    BOOL needsFirstResponderDance = self.isFirstResponder && _keyboardAppearance != keyboardAppearance;
    if (needsFirstResponderDance) {
        [self resignFirstResponder];
    }
    _keyboardAppearance = keyboardAppearance;
    if (needsFirstResponderDance) {
        [self becomeFirstResponder];
    }
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
// Apparently required on iOS 15+: https://stackoverflow.com/a/72359764
- (UITextSpellCheckingType)spellCheckingType {
    return UITextSpellCheckingTypeNo;
}

#pragma mark Hardware Keyboard

- (void)handleKeyCommand:(UIKeyCommand *)command {
    NSString *key = command.input;
    if (@available(iOS 13.0, *)) {
        if ( command.propertyList != nil ) {
            [self insertRawText:command.propertyList];
            return;
        }
    }
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
static const char *controlKeys = "abcdefghijklmnopqrstuvwxyz@^26-=[]\\ ";
static const char *metaKeys = "abcdefghijklmnopqrstuvwxyz0123456789-=[]\\;',./";

- (NSArray<UIKeyCommand *> *)keyCommands {
    if (_keyCommands != nil)
        return _keyCommands;
    _keyCommands = [NSMutableArray new];
    [self addKeys:controlKeys withModifiers:UIKeyModifierControl];

    if (@available(iOS 13.4, *)) {
        [self addFunctionKey:UIKeyInputUpArrow withName:@"Up" withNormalEscapeSequence:@"\x1b[A" withShiftEscapeSequence:@"\x1b[1;2A" withControlEscapeSequence:@"\x1b[1;5A"];
        [self addFunctionKey:UIKeyInputDownArrow withName:@"Down" withNormalEscapeSequence:@"\x1b[B" withShiftEscapeSequence:@"\x1b[1;2B" withControlEscapeSequence:@"\x1b[1;5B"];
        [self addFunctionKey:UIKeyInputRightArrow withName:@"Right" withNormalEscapeSequence:@"\x1b[C" withShiftEscapeSequence:@"\x1b[1;2C" withControlEscapeSequence:@"\x1b[1;5C"];
        [self addFunctionKey:UIKeyInputLeftArrow withName:@"Left" withNormalEscapeSequence:@"\x1b[D" withShiftEscapeSequence:@"\x1b[1;2D" withControlEscapeSequence:@"\x1b[1;5D"];
        [self addFunctionKey:@"\t" withName:@"Tab" withNormalEscapeSequence:@"\t" withShiftEscapeSequence:@"\x1b[Z" withControlEscapeSequence:NULL];

        [self addFunctionKey:UIKeyInputEscape withName:@"Esc" withNormalEscapeSequence:@"\x1b" withShiftEscapeSequence:NULL withControlEscapeSequence:NULL];
        // Navigation keys

        /*
         * Now UIKey equivalent for Ins/help keys presumably because Apple Keyboards don't have Ins key :-(  Have to handle these in pressesbegan
        [self addFunctionKey:UIKeyInputInsert withNormalEscapeSequence:@"\x1b[2~" withShiftEscapeSequence:@"\x1b[2;2~" withControlEscapeSequence:@"\x1b[2;5~"];
        [self addFunctionKey:UIKeyInputHelp withNormalEscapeSequence:@"\x1b[2~" withShiftEscapeSequence:@"\x1b[2;2~" withControlEscapeSequence:@"\x1b[2;5~"];
         */
        if (@available(iOS 15.0, *)) {
            [self addFunctionKey:UIKeyInputDelete withName:@"Del" withNormalEscapeSequence:@"\x1b[3~" withShiftEscapeSequence:@"\x1b[3;2~" withControlEscapeSequence:@"\x1b[3;5~"];
        }
        [self addFunctionKey:UIKeyInputPageUp withName:@"PgUp" withNormalEscapeSequence:@"\x1b[5~" withShiftEscapeSequence:@"\x1b[5;2~" withControlEscapeSequence:@"\x1b[5;5~"];
        [self addFunctionKey:UIKeyInputPageDown withName:@"PgDn" withNormalEscapeSequence:@"\x1b[6~" withShiftEscapeSequence:@"\x1b[6;2~" withControlEscapeSequence:@"\x1b[6;5~"];
        [self addFunctionKey:UIKeyInputHome withName:@"Home" withNormalEscapeSequence:@"\x1bOH" withShiftEscapeSequence:@"\x1b[1;2H" withControlEscapeSequence:@"\x1b[1;5H"];
        [self addFunctionKey:UIKeyInputEnd withName:@"End" withNormalEscapeSequence:@"\x1bOF" withShiftEscapeSequence:@"\x1b[1;2F" withControlEscapeSequence:@"\x1b[1;5F"];
        // Function keys
        [self addFunctionKey:UIKeyInputF1 withName:@"F1" withNormalEscapeSequence:@"\x1bOP" withShiftEscapeSequence:@"\x1b[1;2P" withControlEscapeSequence:@"\x1b[1;5P"];
        [self addFunctionKey:UIKeyInputF2 withName:@"F2" withNormalEscapeSequence:@"\x1bOQ" withShiftEscapeSequence:@"\x1b[1;2Q" withControlEscapeSequence:@"\x1b[1;5Q"];
        [self addFunctionKey:UIKeyInputF3 withName:@"F3" withNormalEscapeSequence:@"\x1bOR" withShiftEscapeSequence:@"\x1b[1;2R" withControlEscapeSequence:@"\x1b[1;5R"];
        [self addFunctionKey:UIKeyInputF4 withName:@"F4" withNormalEscapeSequence:@"\x1bOS" withShiftEscapeSequence:@"\x1b[1;2S" withControlEscapeSequence:@"\x1b[1;5S"];
        [self addFunctionKey:UIKeyInputF5 withName:@"F5" withNormalEscapeSequence:@"\x1b[15~" withShiftEscapeSequence:@"\x1b[15;2~" withControlEscapeSequence:@"\x1b[15;5~"];
        // Yes, @"\x1b[16~" is missing; it is meant to be
        [self addFunctionKey:UIKeyInputF6 withName:@"F6" withNormalEscapeSequence:@"\x1b[17~" withShiftEscapeSequence:@"\x1b[17;2~" withControlEscapeSequence:@"\x1b[17;5~"];
        [self addFunctionKey:UIKeyInputF7 withName:@"F7" withNormalEscapeSequence:@"\x1b[18~" withShiftEscapeSequence:@"\x1b[18;2~" withControlEscapeSequence:@"\x1b[18;5~"];
        [self addFunctionKey:UIKeyInputF8 withName:@"F8" withNormalEscapeSequence:@"\x1b[19~" withShiftEscapeSequence:@"\x1b[19;2~" withControlEscapeSequence:@"\x1b[19;5~"];
        [self addFunctionKey:UIKeyInputF9 withName:@"F9" withNormalEscapeSequence:@"\x1b[20~" withShiftEscapeSequence:@"\x1b[20;2~" withControlEscapeSequence:@"\x1b[20;5~"];
        [self addFunctionKey:UIKeyInputF10 withName:@"F10" withNormalEscapeSequence:@"\x1b[21~" withShiftEscapeSequence:@"\x1b[21;2~" withControlEscapeSequence:@"\x1b[21;5~"];
        // Yes, @"\x1b[22~" is missing; it is meant to be
        [self addFunctionKey:UIKeyInputF11 withName:@"F11" withNormalEscapeSequence:@"\x1b[23~" withShiftEscapeSequence:@"\x1b[23;2~" withControlEscapeSequence:@"\x1b[23;5~"];
        [self addFunctionKey:UIKeyInputF12 withName:@"F12" withNormalEscapeSequence:@"\x1b[24~" withShiftEscapeSequence:@"\x1b[24;2~" withControlEscapeSequence:@"\x1b[24;5~"];
    } else {
        for (NSString *specialKey in @[UIKeyInputEscape, UIKeyInputUpArrow, UIKeyInputDownArrow,
                                   UIKeyInputLeftArrow, UIKeyInputRightArrow, @"\t"]) {
        [self addKey:specialKey withModifiers:0];
        }
    }
    if (UserPreferences.shared.capsLockMapping != CapsLockMapNone) {
        if (@available(iOS 13, *)); else {
            [self addKeys:controlKeys withModifiers:UIKeyModifierAlphaShift];
            [self addKeys:alphabet withModifiers:0];
            [self addKeys:alphabet withModifiers:UIKeyModifierShift];
            [self addKey:@"" withModifiers:UIKeyModifierAlphaShift]; // otherwise tap of caps lock can switch layouts
        }
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
    if (@available(iOS 15, *)) {
        command.wantsPriorityOverSystemBehavior = YES;
    }

    [_keyCommands addObject:command];
}

- (void)addFunctionKey:(NSString *)keyName withName:(NSString *)keyTitle withNormalEscapeSequence:(NSString *)normalEscapeSequence withShiftEscapeSequence:(NSString *)shiftEscapeSequence withControlEscapeSequence:(NSString *)controlEscapeSequence API_AVAILABLE(ios(13.4)) {

    UIKeyCommand *command;

    command = [UIKeyCommand commandWithTitle: @"" image: nil action:@selector(handleKeyCommand:) input: keyName modifierFlags:0 propertyList:normalEscapeSequence];
    if (@available(iOS 15, *)) {
        command.wantsPriorityOverSystemBehavior = YES;
    }
    [_keyCommands addObject:command];

    command = [UIKeyCommand commandWithTitle: @"" image: nil action:@selector(handleKeyCommand:) input: keyName modifierFlags:UIKeyModifierShift propertyList:shiftEscapeSequence];
    if (@available(iOS 15, *)) {
        command.wantsPriorityOverSystemBehavior = YES;
    }
    [_keyCommands addObject:command];

    command = [UIKeyCommand commandWithTitle: @"" image: nil action:@selector(handleKeyCommand:) input: keyName modifierFlags:UIKeyModifierControl propertyList:controlEscapeSequence];
    if (@available(iOS 15, *)) {
        command.wantsPriorityOverSystemBehavior = YES;
    }
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

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    bool handled = false;
    UIKeyModifierFlags modifier;

    if (@available(iOS 13.4, *)) {
        // this is all to handle Ins/Help key as Apple don't support that key using UIKey interface
        UIKey *key;

        for (UIPress *aPress in presses) {
            key = aPress.key;
            handled = false;
            // Use of UIKeyboardHID was introduced in 13.4

            // ignore modifier keys by themselves
            if ( key.keyCode == UIKeyboardHIDUsageKeyboardLeftShift || key.keyCode == UIKeyboardHIDUsageKeyboardLeftControl || key.keyCode == UIKeyboardHIDUsageKeyboardLeftAlt || key.keyCode == UIKeyboardHIDUsageKeyboardRightShift || key.keyCode == UIKeyboardHIDUsageKeyboardRightControl || key.keyCode == UIKeyboardHIDUsageKeyboardRightAlt || key.keyCode == UIKeyboardHIDUsageKeyboardRightGUI ) {
                continue;
            }

            modifier = key.modifierFlags;
            if ( modifier & UIKeyModifierNumericPad ) {
                modifier &= ~UIKeyModifierNumericPad;
            }
            if ( modifier & UIKeyModifierAlphaShift) {
                modifier &= ~UIKeyModifierAlphaShift;
            }
            if ( key.keyCode == UIKeyboardHIDUsageKeyboardInsert || key.keyCode == UIKeyboardHIDUsageKeyboardHelp ) {
                if ( modifier == 0) {
                    [self insertRawText:@"\x1b[2~"];
                    handled = true;
                    break;
                }
                if ( modifier & UIKeyModifierShift )  {
                    [self insertRawText:@"\x1b[2;2~"];
                    handled = true;
                    break;
                }
                if ( modifier & UIKeyModifierControl )  {
                    [self insertRawText:@"\x1b[2;5~"];
                    handled = true;
                    break;
                }
            }
        }
    }
    if ( !handled) {
       return [super pressesBegan:presses withEvent:event];
    }
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
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
