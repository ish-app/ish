//
//  APKServer.m
//  iSH
//
//  Created by Theodore Dubois on 11/21/20.
//

#import <CFNetwork/CFNetwork.h>
#import <sys/socket.h>
#import <netinet/in.h>
#import "APKServer.h"

@interface APKServer ()

@property NSFileHandle *server;
@property CFMutableDictionaryRef messages;

@end

@implementation APKServer

- (instancetype)init {
    if (self = [super init]) {
        self.messages = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        [self start];
    }
    return self;
}

- (void)start {
    CFSocketRef socket = CFSocketCreate(kCFAllocatorDefault, AF_INET, SOCK_STREAM, 0, 0, NULL, NULL);
    if (socket == nil) {
        NSLog(@"APK server: failed to create socket");
        return;
    }
    int one = 1;
    if (setsockopt(CFSocketGetNative(socket), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
        NSLog(@"APK server: failed to set SO_REUSEADDR");
        return;
    }
    struct sockaddr_in sin = {
        .sin_len = sizeof(sin),
        .sin_family = AF_INET,
        .sin_port = htons(42069), // highly nice
        .sin_addr = INADDR_ANY,
    };
    NSData *sin_data = [NSData dataWithBytes:&sin length:sizeof(sin)];
    if (CFSocketSetAddress(socket, (CFDataRef) sin_data) != kCFSocketSuccess) {
        NSLog(@"APK server: failed to bind");
        return;
    }
    self.server = [[NSFileHandle alloc] initWithFileDescriptor:CFSocketGetNative(socket) closeOnDealloc:YES];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(onAccept:) name:NSFileHandleConnectionAcceptedNotification object:self.server];
    [self.server acceptConnectionInBackgroundAndNotify];
    NSLog(@"APK server: serving");
}

- (void)onAccept:(NSNotification *)n {
    NSFileHandle *client = n.userInfo[NSFileHandleNotificationFileHandleItem];
    CFDictionaryAddValue(self.messages, (__bridge CFTypeRef) client, CFHTTPMessageCreateEmpty(kCFAllocatorDefault, YES));
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(onReadable:) name:NSFileHandleDataAvailableNotification object:client];
    [client waitForDataInBackgroundAndNotify];
    [self.server acceptConnectionInBackgroundAndNotify];
}

- (void)onReadable:(NSNotification *)n {
    NSFileHandle *client = n.object;
    CFHTTPMessageRef message = (CFHTTPMessageRef) CFDictionaryGetValue(self.messages, (__bridge CFTypeRef) client);
    NSData *data = [client availableData];
    if (data.length == 0)
        return [self endRequest:client];
    if (!CFHTTPMessageAppendBytes(message, data.bytes, data.length))
        return [self endRequest:client];
    if (CFHTTPMessageIsHeaderComplete(message))
        return [self sendResponseForRequest:message toClient:client];
    [client waitForDataInBackgroundAndNotify];
}

- (void)sendResponseForRequest:(CFHTTPMessageRef)message toClient:(NSFileHandle *)client {
    NSURL *url = CFBridgingRelease(CFHTTPMessageCopyRequestURL(message));
    NSArray<NSString *> *components = url.pathComponents;
    if ([components.firstObject isEqualToString:@"/"])
        components = [components subarrayWithRange:NSMakeRange(1, components.count - 1)];
    NSString *resourceTag = [components componentsJoinedByString:@":"];

    NSLog(@"APK server: fetching %@", resourceTag);
    NSBundleResourceRequest *request = [[NSBundleResourceRequest alloc] initWithTags:[NSSet setWithObject:resourceTag]];
    request.loadingPriority = NSBundleResourceRequestLoadingPriorityUrgent;
    [request beginAccessingResourcesWithCompletionHandler:^(NSError * _Nullable error) {
        int status = 200;
        NSData *body = nil;
        NSFileHandle *bodyFile = nil;
        NSUInteger contentLength = 0;

        if (error == nil) {
            NSLog(@"APK server: serving %@", resourceTag);
            bodyFile = [NSFileHandle fileHandleForReadingFromURL:[request.bundle URLForResource:resourceTag withExtension:nil] error:&error];
            [bodyFile seekToEndOfFile];
            contentLength = bodyFile.offsetInFile;
            [bodyFile seekToFileOffset:0];
        }
        if (error != nil) {
            NSLog(@"APK server: failed to fetch %@: %@", resourceTag, error);
            status = 500;
            body = [error.description dataUsingEncoding:NSUTF8StringEncoding];
        }

        CFHTTPMessageRef response = CFHTTPMessageCreateResponse(kCFAllocatorDefault, status, NULL, kCFHTTPVersion1_1);
        if (body) {
            CFHTTPMessageSetBody(response, (__bridge CFDataRef) body);
            contentLength = body.length;
        }
        CFHTTPMessageSetHeaderFieldValue(response, CFSTR("Content-Length"), (__bridge CFStringRef) [NSString stringWithFormat:@"%lu", contentLength]);
        [client writeData:CFBridgingRelease(CFHTTPMessageCopySerializedMessage(response))];
        CFRelease(response);

        if (bodyFile) {
            NSData *data;
            int i = 0;
            do {
                data = [bodyFile readDataOfLength:512*1024];
                [client writeData:data];
                i += data.length;
                NSLog(@"%@ %d", url, i);
            } while (data.length);
        }

        [request endAccessingResources];
        [self endRequest:client];
    }];
}

- (void)endRequest:(NSFileHandle *)client {
    [NSNotificationCenter.defaultCenter removeObserver:self name:NSFileHandleDataAvailableNotification object:client];
    CFDictionaryRemoveValue(self.messages, (__bridge CFTypeRef) client);
    [client closeFile];
}

@end
