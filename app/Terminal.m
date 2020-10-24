//
//  Terminal.m
//  iSH
//
//  Created by Theodore Dubois on 10/18/17.
//

#import "Terminal.h"
#import "DelayedUITask.h"
#import "UserPreferences.h"
#include "fs/devices.h"
#include "fs/tty.h"
#include "fs/devices.h"

extern struct tty_driver ios_pty_driver;

@interface Terminal () <WKScriptMessageHandler>

@property WKWebView *webView;
@property BOOL loaded;
@property (nonatomic) struct tty *tty;
@property NSMutableData *pendingData;

@property DelayedUITask *refreshTask;
@property DelayedUITask *scrollToBottomTask;

@property BOOL applicationCursor;

@property NSNumber *terminalsKey;
@property NSUUID *uuid;

@end

@interface CustomWebView : WKWebView
@end
@implementation CustomWebView
- (BOOL)becomeFirstResponder {
    if (@available(iOS 13.4, *)) {
        return [super becomeFirstResponder];
    }
    return NO;
}

- (BOOL)canPerformAction:(SEL)action withSender:(id)sender {
    if (action == @selector(copy:) || action == @selector(paste:)) {
        return NO;
    }
    return [super canPerformAction:action withSender:sender];
}
@end

@implementation Terminal

static NSMapTable<NSNumber *, Terminal *> *terminals;
static NSMapTable<NSUUID *, Terminal *> *terminalsByUUID;

- (instancetype)initWithType:(int)type number:(int)num {
    self.terminalsKey = @(dev_make(type, num));
    Terminal *terminal = [terminals objectForKey:self.terminalsKey];
    if (terminal)
        return terminal;
    
    if (self = [super init]) {
        self.pendingData = [NSMutableData new];
        self.refreshTask = [[DelayedUITask alloc] initWithTarget:self action:@selector(refresh)];
        self.scrollToBottomTask = [[DelayedUITask alloc] initWithTarget:self action:@selector(scrollToBottom)];
        
        WKWebViewConfiguration *config = [WKWebViewConfiguration new];
        [config.userContentController addScriptMessageHandler:self name:@"load"];
        [config.userContentController addScriptMessageHandler:self name:@"log"];
        [config.userContentController addScriptMessageHandler:self name:@"sendInput"];
        [config.userContentController addScriptMessageHandler:self name:@"resize"];
        [config.userContentController addScriptMessageHandler:self name:@"propUpdate"];
        // Make the web view really big so that if a program tries to write to the terminal before it's displayed, the text probably won't wrap too badly.
        CGRect webviewSize = CGRectMake(0, 0, 10000, 10000);
        self.webView = [[CustomWebView alloc] initWithFrame:webviewSize configuration:config];
        self.webView.scrollView.scrollEnabled = NO;
        NSURL *xtermHtmlFile = [NSBundle.mainBundle URLForResource:@"term" withExtension:@"html"];
        [self.webView loadFileURL:xtermHtmlFile allowingReadAccessToURL:xtermHtmlFile];
        
        [terminals setObject:self forKey:self.terminalsKey];
        self.uuid = [NSUUID UUID];
        [terminalsByUUID setObject:self forKey:self.uuid];
    }
    return self;
}

+ (Terminal *)createPseudoTerminal:(struct tty **)tty {
    *tty = pty_open_fake(&ios_pty_driver);
    if (IS_ERR(*tty))
        return nil;
    return (__bridge Terminal *) (*tty)->data;
}

- (void)setTty:(struct tty *)tty {
    _tty = tty;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self syncWindowSize];
    });
}

- (void)userContentController:(WKUserContentController *)userContentController didReceiveScriptMessage:(WKScriptMessage *)message {
    if ([message.name isEqualToString:@"load"]) {
        self.loaded = YES;
        [self.refreshTask schedule];
        // make sure this setting works if it's set before loading
        self.enableVoiceOverAnnounce = self.enableVoiceOverAnnounce;
    } else if ([message.name isEqualToString:@"log"]) {
        NSLog(@"%@", message.body);
    } else if ([message.name isEqualToString:@"sendInput"]) {
        NSData *data = [message.body dataUsingEncoding:NSUTF8StringEncoding];
        [self sendInput:data.bytes length:data.length];
    } else if ([message.name isEqualToString:@"resize"]) {
        [self syncWindowSize];
    } else if ([message.name isEqualToString:@"propUpdate"]) {
        [self setValue:message.body[1] forKey:message.body[0]];
    }
}

- (void)syncWindowSize {
    [self.webView evaluateJavaScript:@"exports.getSize()" completionHandler:^(NSArray<NSNumber *> *dimensions, NSError *error) {
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

- (void)setEnableVoiceOverAnnounce:(BOOL)enableVoiceOverAnnounce {
    _enableVoiceOverAnnounce = enableVoiceOverAnnounce;
    [self.webView evaluateJavaScript:[NSString stringWithFormat:@"term.setAccessibilityEnabled(%@)",
                                      enableVoiceOverAnnounce ? @"true" : @"false"]
                   completionHandler:nil];
}

- (int)write:(const void *)buf length:(size_t)len {
    @synchronized (self) {
        [self.pendingData appendData:[NSData dataWithBytes:buf length:len]];
        [self.refreshTask schedule];
    }
    return 0;
}

- (void)sendInput:(const char *)buf length:(size_t)len {
    if (self.tty == NULL)
        return;
    tty_input(self.tty, buf, len, 0);
    [self.webView evaluateJavaScript:@"exports.setUserGesture()" completionHandler:nil];
    [self.scrollToBottomTask schedule];
}

- (void)scrollToBottom {
    [self.webView evaluateJavaScript:@"exports.scrollToBottom()" completionHandler:nil];
}

- (NSString *)arrow:(char)direction {
    return [NSString stringWithFormat:@"\x1b%c%c", self.applicationCursor ? 'O' : '[', direction];
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
    if (!self.loaded)
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
    NSString *jsToEvaluate = [NSString stringWithFormat:@"exports.write(%@[0])", json];
    [self.webView evaluateJavaScript:jsToEvaluate completionHandler:nil];
}

+ (void)convertCommand:(NSArray<NSString *> *)command toArgs:(char *)argv limitSize:(size_t)maxSize {
    char *p = argv;
    for (NSString *cmd in command) {
        const char *c = cmd.UTF8String;
        // Save space for the final NUL byte in argv
        while (p < argv + maxSize - 1 && (*p++ = *c++));
        // If we reach the end of the buffer, the last string still needs to be
        // NUL terminated
        *p = '\0';
    }
    // Add the final NUL byte to argv
    *++p = '\0';
}

+ (Terminal *)terminalWithType:(int)type number:(int)number {
    return [[Terminal alloc] initWithType:type number:number];
}

+ (Terminal *)terminalWithUUID:(NSUUID *)uuid {
    return [terminalsByUUID objectForKey:uuid];
}

- (void)destroy {
    struct tty *tty = self.tty;
    if (tty != NULL) {
        lock(&tty->lock);
        tty_hangup(tty);
        unlock(&tty->lock);
    }
    [terminals removeObjectForKey:self.terminalsKey];
}

+ (void)initialize {
    terminals = [NSMapTable strongToWeakObjectsMapTable];
    terminalsByUUID = [NSMapTable strongToWeakObjectsMapTable];
}

@end

static int ios_tty_init(struct tty *tty) {
    // This is called with ttys_lock but that results in deadlock since the main thread can also acquire ttys_lock. So release it.
    unlock(&ttys_lock);
    void (^init_block)(void) = ^{
        Terminal *terminal = [Terminal terminalWithType:tty->type number:tty->num];
        tty->data = (void *) CFBridgingRetain(terminal);
        terminal.tty = tty;
    };
    if ([NSThread isMainThread])
        init_block();
    else
        dispatch_sync(dispatch_get_main_queue(), init_block);

    lock(&ttys_lock);
    return 0;
}

static int ios_tty_write(struct tty *tty, const void *buf, size_t len, bool blocking) {
    Terminal *terminal = (__bridge Terminal *) tty->data;
    return [terminal write:buf length:len];
}

static void ios_tty_cleanup(struct tty *tty) {
    Terminal *terminal = CFBridgingRelease(tty->data);
    tty->data = NULL;
    terminal.tty = NULL;
}

struct tty_driver_ops ios_tty_ops = {
    .init = ios_tty_init,
    .write = ios_tty_write,
    .cleanup = ios_tty_cleanup,
};
DEFINE_TTY_DRIVER(ios_console_driver, &ios_tty_ops, TTY_CONSOLE_MAJOR, 64);
struct tty_driver ios_pty_driver = {.ops = &ios_tty_ops};
