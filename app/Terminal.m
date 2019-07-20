//
//  Terminal.m
//  iSH
//
//  Created by Theodore Dubois on 10/18/17.
//

#include <iconv.h>
#import "Terminal.h"
#import "DelayedUITask.h"
#import "UserPreferences.h"
#include "fs/tty.h"

@interface Terminal () <WKScriptMessageHandler>

@property WKWebView *webView;
@property (nonatomic) struct tty *tty;
@property NSMutableData *pendingData;

@property DelayedUITask *refreshTask;
@property DelayedUITask *scrollToBottomTask;

@property BOOL applicationCursor;

@end

@interface CustomWebView : WKWebView
@end
@implementation CustomWebView
- (BOOL)becomeFirstResponder {
    return NO;
}
@end

@implementation Terminal

static NSMutableDictionary<NSNumber *, Terminal *> *terminals;

- (instancetype)initWithType:(int)type number:(int)num {
    NSNumber *key = @(dev_make(type, num));
    Terminal *terminal = [terminals objectForKey:key];
    if (terminal)
        return terminal;
    
    if (self = [super init]) {
        self.pendingData = [NSMutableData new];
        self.refreshTask = [[DelayedUITask alloc] initWithTarget:self action:@selector(refresh)];
        self.scrollToBottomTask = [[DelayedUITask alloc] initWithTarget:self action:@selector(scrollToBottom)];
        
        WKWebViewConfiguration *config = [WKWebViewConfiguration new];
        [config.userContentController addScriptMessageHandler:self name:@"log"];
        [config.userContentController addScriptMessageHandler:self name:@"resize"];
        [config.userContentController addScriptMessageHandler:self name:@"propUpdate"];
        // Make the web view really big so that if a program tries to write to the terminal before it's displayed, the text probably won't wrap too badly.
        CGRect webviewSize = CGRectMake(0, 0, 10000, 10000);
        self.webView = [[CustomWebView alloc] initWithFrame:webviewSize configuration:config];
        self.webView.scrollView.scrollEnabled = NO;
        NSURL *xtermHtmlFile = [NSBundle.mainBundle URLForResource:@"xterm-dist/term" withExtension:@"html"];
        [self.webView loadFileURL:xtermHtmlFile allowingReadAccessToURL:xtermHtmlFile];
        [self.webView addObserver:self forKeyPath:@"loading" options:0 context:NULL];
        [self _addPreferenceObservers];
        
        [terminals setObject:self forKey:key];
    }
    return self;
}

- (void)setTty:(struct tty *)tty {
    _tty = tty;
    [self syncWindowSize];
}

- (void)userContentController:(WKUserContentController *)userContentController didReceiveScriptMessage:(WKScriptMessage *)message {
    if ([message.name isEqualToString:@"log"]) {
        NSLog(@"%@", message.body);
    } else if ([message.name isEqualToString:@"resize"]) {
        [self syncWindowSize];
    } else if ([message.name isEqualToString:@"propUpdate"]) {
        [self setValue:message.body[1] forKey:message.body[0]];
    }
}

- (void)syncWindowSize {
    [self.webView evaluateJavaScript:@"[term.cols, term.rows]" completionHandler:^(NSArray<NSNumber *> *dimensions, NSError *error) {
        if (self.tty == NULL) {
            return;
        }
        int cols = dimensions[0].intValue;
        int rows = dimensions[1].intValue;
        lock(&self.tty->lock);
        tty_set_winsize(self.tty, (struct winsize_) {.col = cols, .row = rows});
        unlock(&self.tty->lock);
    }];
}

- (int)write:(const void *)buf length:(size_t)len {
    @synchronized (self) {
        [self.pendingData appendData:[NSData dataWithBytes:buf length:len]];
        [self.refreshTask schedule];
    }
    return 0;
}

- (void)sendInput:(const char *)buf length:(size_t)len {
    tty_input(self.tty, buf, len, 0);
    [self.scrollToBottomTask schedule];
}

- (void)scrollToBottom {
    [self.webView evaluateJavaScript:@"term.scrollToBottom()" completionHandler:nil];
}

- (NSString *)arrow:(char)direction {
    return [NSString stringWithFormat:@"\x1b%c%c", self.applicationCursor ? 'O' : '[', direction];
}

- (void)_addPreferenceObservers {
    UserPreferences *prefs = [UserPreferences shared];
    NSKeyValueObservingOptions opts = NSKeyValueObservingOptionNew;
    [prefs addObserver:self forKeyPath:@"fontSize" options:opts context:nil];
    [prefs addObserver:self forKeyPath:@"theme" options:opts context:nil];
}

- (NSString *)cssColor:(UIColor *)color {
    CGFloat red, green, blue, alpha;
    [color getRed:&red green:&green blue:&blue alpha:&alpha];
    return [NSString stringWithFormat:@"rgba(%ld, %ld, %ld, %ld)",
            lround(red * 255), lround(green * 255), lround(blue * 255), lround(alpha * 255)];
}

- (void)_updateStyleFromPreferences {
    UserPreferences *prefs = [UserPreferences shared];
    id themeInfo = @{
                     @"fontSize": prefs.fontSize,
                     @"foregroundColor": [self cssColor:prefs.theme.foregroundColor],
                     @"backgroundColor": [self cssColor:prefs.theme.backgroundColor],
                     };
    NSString *json = [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:themeInfo options:0 error:nil] encoding:NSUTF8StringEncoding];
    [self.webView evaluateJavaScript:[NSString stringWithFormat:@"updateStyle(%@)", json] completionHandler:nil];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    if (object == self.webView && [keyPath isEqualToString:@"loading"] && !self.webView.loading) {
        [self _updateStyleFromPreferences];
        [self.refreshTask schedule];
        [self.webView removeObserver:self forKeyPath:@"loading"];
    } else if (object == [UserPreferences shared]) {
        [self _updateStyleFromPreferences];
    }
}

NSData *removeInvalidUTF8(NSData *data) {
    static const uint32_t mins[4] = {0, 128, 2048, 65536};
    NSMutableData *cleanData = [NSMutableData dataWithLength:data.length];
    const uint8_t *bytes = data.bytes;
    uint8_t *clean_bytes = cleanData.mutableBytes;
    size_t clean_length = 0;
    size_t clean_i = 0;
    unsigned continuations = 0;
    uint32_t c = 0;
    uint32_t min_c = 0;
    for (size_t i = 0; i < data.length; i++) {
        if (bytes[i] >> 6 != 0b10) {
            // start of new sequence
            if (continuations != 0)
                goto discard;
            if (bytes[i] >> 7 == 0b0) {
                continuations = 0;
                c = bytes[i] & 0b1111111;
            } else if (bytes[i] >> 5 == 0b110) {
                continuations = 1;
                c = bytes[i] & 0b11111;
            } else if (bytes[i] >> 4 == 0b1110) {
                continuations = 2;
                c = bytes[i] & 0b1111;
            } else if (bytes[i] >> 3 == 0b11110) {
                continuations = 3;
                c = bytes[i] & 0b111;
            } else {
                goto discard;
            }
            min_c = mins[continuations];
        } else {
            // continuation
            if (continuations == 0)
                goto discard;
            continuations--;
            c = (c << 6) | (bytes[i] & 0b111111);
        }
        clean_bytes[clean_i++] = bytes[i];
        if (continuations == 0) {
            if (c < min_c || c > 0x10FFFF)
                goto discard; // out of range
            if ((c >> 11) == 0x1b)
                goto discard; // surrogate pair (this isn't cesu8)
            clean_length = clean_i;
        }
        continue;
        
    discard:
        // if we were in the middle of the sequence, see if this byte could start a sequence
        if (clean_i != clean_length)
            i--;
        clean_i = clean_length;
        continuations = 0;
    }
    cleanData.length = clean_length;
    return cleanData;
}

- (void)refresh {
    if (self.webView.loading)
        return;
    
    NSData *data;
    @synchronized (self) {
        data = self.pendingData;
        self.pendingData = [NSMutableData new];
    }
    NSData *cleanData = removeInvalidUTF8(data);
    NSString *str = [[NSString alloc] initWithData:cleanData encoding:NSUTF8StringEncoding];
    
    NSError *err = nil;
    NSData *jsonData = [NSJSONSerialization dataWithJSONObject:@[str] options:0 error:&err];
    NSString *json = [[NSString alloc] initWithData:jsonData encoding:NSUTF8StringEncoding];
    NSAssert(err == nil, @"JSON serialization failed, wtf");
    NSString *jsToEvaluate = [NSString stringWithFormat:@"termWrite(%@[0])", json];
    [self.webView evaluateJavaScript:jsToEvaluate completionHandler:nil];
}

+ (Terminal *)terminalWithType:(int)type number:(int)number {
    return [[Terminal alloc] initWithType:type number:number];
}

+ (void)initialize {
    terminals = [NSMutableDictionary new];
}

@end

static int ios_tty_init(struct tty *tty) {
    tty->refcount++;
    void (^init_block)(void) = ^{
        Terminal *terminal = [Terminal terminalWithType:tty->type number:tty->num];
        tty->data = (void *) CFBridgingRetain(terminal);
        terminal.tty = tty;
    };
    if ([NSThread isMainThread])
        init_block();
    else
        dispatch_sync(dispatch_get_main_queue(), init_block);

    // termios
    tty->termios.lflags = ISIG_ | ICANON_ | ECHO_ | ECHOE_ | ECHOCTL_;
    tty->termios.iflags = ICRNL_;
    tty->termios.oflags = OPOST_ | ONLCR_;
    tty->termios.cc[VINTR_] = '\x03';
    tty->termios.cc[VQUIT_] = '\x1c';
    tty->termios.cc[VERASE_] = '\x7f';
    tty->termios.cc[VKILL_] = '\x15';
    tty->termios.cc[VEOF_] = '\x04';
    tty->termios.cc[VTIME_] = 0;
    tty->termios.cc[VMIN_] = 1;
    tty->termios.cc[VSTART_] = '\x11';
    tty->termios.cc[VSTOP_] = '\x13';
    tty->termios.cc[VSUSP_] = '\x1a';
    tty->termios.cc[VEOL_] = 0;
    tty->termios.cc[VREPRINT_] = '\x12';
    tty->termios.cc[VDISCARD_] = '\x0f';
    tty->termios.cc[VWERASE_] = '\x17';
    tty->termios.cc[VLNEXT_] = '\x16';
    tty->termios.cc[VEOL2_] = 0;

    return 0;
}

static int ios_tty_write(struct tty *tty, const void *buf, size_t len, bool blocking) {
    Terminal *terminal = (__bridge Terminal *) tty->data;
    return [terminal write:buf length:len];
}

static void ios_tty_cleanup(struct tty *tty) {
    CFBridgingRelease(tty->data);
}

struct tty_driver_ops ios_tty_ops = {
    .init = ios_tty_init,
    .write = ios_tty_write,
    .cleanup = ios_tty_cleanup,
};
DEFINE_TTY_DRIVER(ios_tty_driver, &ios_tty_ops, 64);
