//
//  FileProviderItem.m
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import <MobileCoreServices/MobileCoreServices.h>
#import "FileProviderExtension.h"
#import "FileProviderItem.h"
#include "kernel/task.h"
#include "kernel/fs.h"
#include "fs/path.h"

@interface FileProviderItem ()

@property NSString *path;
@property (readonly) BOOL isRoot;

@end

@implementation FileProviderItem

- (instancetype)initWithIdentifier:(NSFileProviderItemIdentifier)identifier {
    if (self = [super init]) {
        self.path = identifier;
    }
    return self;
}

- (BOOL)isRoot {
    return [self.path isEqualToString:NSFileProviderRootContainerItemIdentifier];
}

// TODO: implement an initializer to create an item from your extension's backing model
// TODO: implement the accessors to return the values from your extension's backing model

- (NSFileProviderItemIdentifier)itemIdentifier {
    NSLog(@"id of %@ is %@", self.path, self.path);
    return self.path;
}
- (NSFileProviderItemIdentifier)parentItemIdentifier {
    if (self.isRoot) {
        NSLog(@"parent of %@ is %@", self.path, NSFileProviderRootContainerItemIdentifier);
        return NSFileProviderRootContainerItemIdentifier;
    }
    if ([self.path.stringByDeletingLastPathComponent isEqualToString:@"/"]) {
        NSLog(@"parent of %@ is %@", self.path, NSFileProviderRootContainerItemIdentifier);
        return NSFileProviderRootContainerItemIdentifier;
    }
    NSLog(@"parent of %@ is %@", self.path, self.path.stringByDeletingLastPathComponent);
    return self.path.stringByDeletingLastPathComponent;
}

- (NSFileProviderItemCapabilities)capabilities {
    return NSFileProviderItemCapabilitiesAllowsReading;
}

- (NSString *)filename {
    if (self.isRoot) {
        NSLog(@"filename of %@ is %@", self.path, @"/");
        return @"/";
    }
    NSLog(@"filename of %@ is %@", self.path, self.path.lastPathComponent);
    return self.path.lastPathComponent;
}

- (NSString *)typeIdentifier {
    if (self.isRoot) {
        NSLog(@"uti of %@ is %@", self.path, (NSString *) kUTTypeDirectory);
        return (NSString *) kUTTypeFolder;
    }
    current = fake_task;
    struct statbuf stat;
    int err = generic_statat(AT_PWD, self.path.UTF8String, &stat, true);
    if (err < 0) {
        NSLog(@"stat error %d", err);
        return (NSString *) kUTTypeData;
    }
    if ((stat.mode & S_IFMT) == S_IFDIR)
        return (NSString *) kUTTypeFolder;
    if ((stat.mode & S_IFMT) == S_IFLNK)
        return (NSString *) kUTTypeSymLink;
    NSString *uti = CFBridgingRelease(UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension,
                                                                            (__bridge CFStringRef _Nonnull) self.path.pathExtension, nil));
    NSLog(@"uti of %@ is %@", self.path, uti);
    return uti;
}

@end
