//
//  Terminal.m
//  iSH
//
//  Created by Theodore Dubois on 10/18/17.
//

#include <iconv.h>
#import "Terminal.h"
#import "DelayedUITask.h"
#include "fs/tty.h"

@interface Terminal () <WKScriptMessageHandler>

@property WKWebView *webView;
@property (nonatomic) struct tty *tty;
@property NSMutableData *pendingData;

@property DelayedUITask *refreshTask;
@property DelayedUITask *scrollToBottomTask;

@end

@implementation Terminal

static Terminal *terminal = nil;

- (instancetype)init {
    if (terminal)
        return terminal;
    if (self = [super init]) {
        self.pendingData = [NSMutableData new];
        self.refreshTask = [[DelayedUITask alloc] initWithTarget:self action:@selector(refresh)];
        self.scrollToBottomTask = [[DelayedUITask alloc] initWithTarget:self action:@selector(scrollToBottom)];
        
        WKWebViewConfiguration *config = [WKWebViewConfiguration new];
        [config.userContentController addScriptMessageHandler:self name:@"log"];
        [config.userContentController addScriptMessageHandler:self name:@"resize"];
        self.webView = [[WKWebView alloc] initWithFrame:CGRectZero configuration:config];
        self.webView.scrollView.scrollEnabled = NO;
        [self.webView loadRequest:
         [NSURLRequest requestWithURL:
          [NSBundle.mainBundle URLForResource:@"xterm-dist/term" withExtension:@"html"]]];
        [self.webView addObserver:self forKeyPath:@"loading" options:0 context:NULL];
        terminal = self;
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
    }
}

- (void)syncWindowSize {
    NSLog(@"syncing");
    [self.webView evaluateJavaScript:@"[term.cols, term.rows]" completionHandler:^(NSArray<NSNumber *> *dimensions, NSError *error) {
        if (self.tty == NULL) {
            NSLog(@"gave up");
            return;
        }
        int cols = dimensions[0].intValue;
        int rows = dimensions[1].intValue;
        NSLog(@"%dx%d", cols, rows);
        lock(&self.tty->lock);
        tty_set_winsize(self.tty, (struct winsize_) {.col = cols, .row = rows});
        unlock(&self.tty->lock);
    }];
}

- (size_t)write:(const void *)buf length:(size_t)len {
    @synchronized (self) {
        [self.pendingData appendData:[NSData dataWithBytes:buf length:len]];
        [self.refreshTask schedule];
    }
    return len;
}

- (void)sendInput:(const char *)buf length:(size_t)len {
    tty_input(self.tty, buf, len);
    [self.scrollToBottomTask schedule];
}

- (void)scrollToBottom {
    [self.webView evaluateJavaScript:@"term.scrollToBottom()" completionHandler:nil];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    if (object == self.webView && [keyPath isEqualToString:@"loading"] && !self.webView.loading) {
        [self.refreshTask schedule];
        [self.webView removeObserver:self forKeyPath:@"loading"];
    }
}

- (void)refresh {
    if (self.webView.loading)
        return;
    
    NSData *data;
    @synchronized (self) {
        data = self.pendingData;
        self.pendingData = [NSMutableData new];
    }
    // character encoding hell
    NSMutableData *cleanData = [NSMutableData dataWithLength:data.length];
    iconv_t conv = iconv_open("UTF-8", "UTF-8");
    BOOL yes = YES;
    iconvctl(conv, ICONV_SET_DISCARD_ILSEQ, &yes);
    const char *dataBytes = data.bytes;
    char *cleanDataBytes = cleanData.mutableBytes;
    size_t dataLength = data.length;
    size_t cleanDataLength = cleanData.length;
    iconv(conv, (char **) &dataBytes, &dataLength, &cleanDataBytes, &cleanDataLength);
    iconv_close(conv);
    cleanData.length -= cleanDataLength;
    NSString *str = [[NSString alloc] initWithData:cleanData encoding:NSUTF8StringEncoding];
    
    NSError *err = nil;
    NSData *jsonData = [NSJSONSerialization dataWithJSONObject:@[str] options:0 error:&err];
    NSString *json = [[NSString alloc] initWithData:jsonData encoding:NSASCIIStringEncoding];
    NSAssert(err == nil, @"JSON serialization failed, wtf");
    NSString *jsToEvaluate = [NSString stringWithFormat:@"term.write(%@[0])", json];
    [self.webView evaluateJavaScript:jsToEvaluate completionHandler:nil];
}

+ (Terminal *)terminalWithType:(int)type number:(int)number {
    return [Terminal new];
}

@end

static int ios_tty_open(struct tty *tty) {
    Terminal *terminal = [Terminal terminalWithType:tty->type number:tty->num];
    terminal.tty = tty;
    tty->data = (void *) CFBridgingRetain(terminal);

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

static ssize_t ios_tty_write(struct tty *tty, const void *buf, size_t len) {
    Terminal *terminal = (__bridge Terminal *) tty->data;
    return [terminal write:buf length:len];
}

static void ios_tty_close(struct tty *tty) {
    CFBridgingRelease(tty->data);
}

struct tty_driver ios_tty_driver = {
    .open = ios_tty_open,
    .write = ios_tty_write,
    .close = ios_tty_close,
};
