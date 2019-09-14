//
//  Terminal.h
//  iSH
//
//  Created by Theodore Dubois on 10/18/17.
//

#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>

@interface Terminal : NSObject

+ (Terminal *)terminalWithType:(int)type number:(int)number;
+ (void)terminalWithTTYNumber:(int)number launchCommand:(NSArray<NSString *> *)command completion:(void (^)(Terminal *))completion;

+ (bool)isTTYNumberFree:(int)number;
+ (int)nextFreeTTYNumber;

+ (void)discardTTYWithNumber:(int)number;

- (int)write:(const void *)buf length:(size_t)len;
- (void)sendInput:(const char *)buf length:(size_t)len;

- (NSString *)arrow:(char)direction;

+ (void)convertCommand:(NSArray<NSString *> *)command toArgs:(char *)argv limitSize:(size_t)maxSize;

@property (readonly) WKWebView *webView;
@property int launchCommandPID;

@end

extern struct tty_driver ios_tty_driver;
