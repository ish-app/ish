//
//  FileProviderItem.m
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#include <sys/stat.h>
#import <MobileCoreServices/MobileCoreServices.h>
#import "FileProviderExtension.h"
#import "FileProviderItem.h"
#import "NSError+ISHErrno.h"
#include "kernel/fs.h"
#define ISH_INTERNAL
#include "fs/fake.h"
#include "kernel/errno.h"

@interface FileProviderItem ()

@property (readonly) NSFileProviderItemIdentifier identifier;
@property (readonly) struct mount *mount;
@property (readonly) struct fd *fd;
@property (readonly) BOOL isRoot;

@end

@implementation FileProviderItem

- (instancetype)initWithIdentifier:(NSFileProviderItemIdentifier)identifier mount:(struct mount *)mount error:(NSError *__autoreleasing  _Nullable *)error {
    if (self = [super init]) {
        _identifier = identifier;
        _mount = mount;
        _fd = [self openNewFDWithError:error];
        if (_fd == NULL)
            return nil;
    }
    return self;
}

- (BOOL)isRoot {
    return [self.identifier isEqualToString:NSFileProviderRootContainerItemIdentifier];
}

- (struct fd *)openNewFDWithError:(NSError *__autoreleasing  _Nullable *)error {
    struct fd *fd;
    if (self.isRoot) {
        fd = self.mount->fs->open(self.mount, "", O_RDONLY_, 0);
    } else {
        fd = fakefs_open_inode(self.mount, self.identifier.longLongValue);
    }
    if (IS_ERR(fd)) {
        NSLog(@"opening %@ failed: %ld", self.identifier, PTR_ERR(fd));
        if (error != nil) {
            *error = [NSError errorWithISHErrno:PTR_ERR(fd) itemIdentifier:self.identifier];
        }
        return NULL;
    }
    fd->mount = self.mount;
    return fd;
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
    NSFileProviderItemCapabilities caps = NSFileProviderItemCapabilitiesAllowsDeleting | NSFileProviderItemCapabilitiesAllowsRenaming | NSFileProviderItemCapabilitiesAllowsReparenting;
    if (S_ISREG(self.stat.mode))
        caps |= NSFileProviderItemCapabilitiesAllowsReading | NSFileProviderItemCapabilitiesAllowsWriting;
    else if (S_ISDIR(self.stat.mode))
        caps |= NSFileProviderItemCapabilitiesAllowsAddingSubItems | NSFileProviderItemCapabilitiesAllowsContentEnumerating;
    else
        return 0;
    return caps;
}

- (NSString *)filename {
    NSString *filename = self.path.lastPathComponent;
    NSLog(@"filename %@", filename);
    return filename;
}

- (NSNumber *)documentSize {
    return [NSNumber numberWithUnsignedLongLong:self.stat.size];
}

- (NSNumber *)childItemCount {
    if (!S_ISDIR(self.stat.mode))
        return @0;
    struct fd *fd = [self openNewFDWithError:nil];
    if (fd == NULL)
        return @0;
    unsigned n = 0;
    struct dir_entry dirent;
    while (fd->ops->readdir(fd, &dirent)) {
        if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0)
            continue;
        n++;
    }
    return @(n);
}

- (NSDate *)contentModificationDate {
    struct statbuf stat = self.stat;
    return [NSDate dateWithTimeIntervalSince1970:stat.mtime + (NSTimeInterval) stat.mtime_nsec / 1000000000];
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
// or at least tries to

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
