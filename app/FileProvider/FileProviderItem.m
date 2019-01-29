//
//  FileProviderItem.m
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import <MobileCoreServices/MobileCoreServices.h>
#import "FileProviderExtension.h"
#import "FileProviderItem.h"
#import "NSError+ISHErrno.h"
#include "kernel/fs.h"
#include "kernel/errno.h"

struct fd *fakefs_open_inode(struct mount *mount, ino_t inode);

@interface FileProviderItem ()

@property (readonly) NSString *path;
@property (readonly) BOOL isRoot;

@end

@implementation FileProviderItem

- (instancetype)initWithIdentifier:(NSFileProviderItemIdentifier)identifier mount:(struct mount *)mount error:(NSError *__autoreleasing  _Nullable *)error {
    if (self = [super init]) {
        struct fd *fd;
        if ([identifier isEqualToString:NSFileProviderRootContainerItemIdentifier]) {
            _isRoot = YES;
            fd = mount->fs->open(mount, "", O_RDONLY_, 0);
        } else {
            ino_t inode = identifier.longLongValue;
            fd = fakefs_open_inode(mount, inode);
        }
        if (IS_ERR(fd)) {
            NSLog(@"opening %@ failed: %ld", identifier, PTR_ERR(fd));
            *error = [NSError errorWithISHErrno:PTR_ERR(fd)];
            return nil;
        }
        fd->mount = mount;
        _fd = fd;
    }
    return self;
}

- (NSString *)path {
    char path[MAX_PATH] = "";
    int err = self.fd->mount->fs->getpath(self.fd, path);
    [self handleError:err inFunction:@"getpath"];
    return [NSString stringWithUTF8String:path];
}

- (NSURL *)URL {
    NSURL *rootURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:self.fd->mount->source]];
    if (self.isRoot)
        return rootURL;
    
    return [rootURL URLByAppendingPathComponent:self.path];
}

- (struct statbuf)stat {
    struct statbuf stat = {};
    int err = self.fd->mount->fs->fstat(self.fd, &stat);
    [self handleError:err inFunction:@"fstat"];
    return stat;
}

- (NSFileProviderItemIdentifier)itemIdentifier {
    if (self.isRoot)
        return NSFileProviderRootContainerItemIdentifier;
    NSString *ident = [NSString stringWithFormat:@"%lu", (unsigned long) self.stat.inode];
    NSLog(@"ident of %@ is %@", self.path, ident);
    return ident;
}
- (NSFileProviderItemIdentifier)parentItemIdentifier {
    if (self.isRoot) {
        NSLog(@"parent of root %@ is %@", self.path, NSFileProviderRootContainerItemIdentifier);
        return NSFileProviderRootContainerItemIdentifier;
    }
    NSString *parentPath = self.path.stringByDeletingLastPathComponent;
    if ([parentPath isEqualToString:@"/"])
        return NSFileProviderRootContainerItemIdentifier;
    struct statbuf stat;
    int err = self.fd->mount->fs->stat(self.fd->mount, parentPath.UTF8String, &stat, false);
    [self handleError:err inFunction:@"stat"];
    NSString *parent = [NSString stringWithFormat:@"%lu", (unsigned long) stat.inode];
    NSLog(@"parent of %@ is %@", self.path, parent);
    return parent;
}

- (NSFileProviderItemCapabilities)capabilities {
    return NSFileProviderItemCapabilitiesAllowsReading;
}

- (NSString *)filename {
    NSString *filename = self.path.lastPathComponent;
    NSLog(@"filename %@", filename);
    return filename;
}

- (NSNumber *)documentSize {
    return [NSNumber numberWithUnsignedLongLong:self.stat.size];
}

- (NSString *)typeIdentifier {
    if (self.isRoot) {
        NSLog(@"uti of %@ is %@", self.path, (NSString *) kUTTypeFolder);
        return (NSString *) kUTTypeFolder;
    }
    mode_t_ mode = self.stat.mode;
    if ((mode & S_IFMT) == S_IFDIR)
        return (NSString *) kUTTypeFolder;
    if ((mode & S_IFMT) == S_IFLNK)
        return (NSString *) kUTTypeSymLink;
    NSString *uti = CFBridgingRelease(UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension,
                                                                            (__bridge CFStringRef _Nonnull) self.path.pathExtension, nil));
    if ([uti hasPrefix:@"dyn."])
        uti = (NSString *) kUTTypePlainText;
    NSLog(@"uti of %@ is %@", self.path, uti);
    return uti;
}

// locking on these keeps the remove/copy operation atomic

- (void)loadToURL:(NSURL *)url {
    NSLog(@"copying %@ to %@", self.path, url);
    NSURL *itemURL = self.URL;
    NSError *err;
    lock(&self.fd->mount->lock);
    [NSFileManager.defaultManager removeItemAtURL:url error:nil];
    BOOL success = [NSFileManager.defaultManager copyItemAtURL:itemURL
                                                         toURL:url
                                                         error:&err];
    unlock(&self.fd->mount->lock);
    if (!success) {
        NSLog(@"error copying to %@: %@", url, err);
    }
}

- (void)saveFromURL:(NSURL *)url {
    NSLog(@"copying %@ from %@", self.path, url);
    NSURL *itemURL = self.URL;
    NSError *err;
    lock(&self.fd->mount->lock);
    [NSFileManager.defaultManager removeItemAtURL:itemURL error:nil];
    BOOL success = [NSFileManager.defaultManager copyItemAtURL:url
                                                         toURL:itemURL
                                                         error:&err];
    unlock(&self.fd->mount->lock);
    if (!success) {
        NSLog(@"error copying to %@: %@", url, err);
    }
}

- (void)dealloc {
    if (self.fd != nil)
        fd_close(self.fd);
}

- (void)handleError:(long)err inFunction:(NSString *)func {
    if (err < 0) {
        NSLog(@"%@ returned %ld", func, err);
        abort();
    }
}

@end
