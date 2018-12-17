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

@interface FileProviderItem ()

@property (readonly) NSString *path;
@property (readonly) BOOL isRoot;

@end


@implementation FileProviderItem

- (instancetype)initWithIdentifier:(NSFileProviderItemIdentifier)identifier mount:(struct mount *)mount error:(NSError *__autoreleasing  _Nullable *)error {
    if (self = [super init]) {
        const char *path;
        struct fd *fd;
        if ([identifier isEqualToString:NSFileProviderRootContainerItemIdentifier]) {
            _isRoot = YES;
            fd = mount->fs->open(mount, "", O_RDONLY_, 0);
        } else {
            ino_t inode = identifier.lastPathComponent.longLongValue;
            lock(&mount->lock);
            sqlite3_stmt *stmt = mount->stmt.path_from_inode;
            sqlite3_bind_int64(stmt, 1, inode);
            int step_res;
        step:
            step_res = sqlite3_step(stmt);
            if (step_res != SQLITE_DONE && step_res != SQLITE_ROW) {
                NSLog(@"sqlite error %s", sqlite3_errmsg(mount->db));
                *error = [NSError errorWithDomain:@"SQLiteErrorDomain" code:step_res userInfo:nil];
                sqlite3_reset(stmt);
                unlock(&mount->lock);
                return nil;
            }
            if (step_res == SQLITE_DONE) {
                *error = [NSError errorWithDomain:NSFileProviderErrorDomain code:NSFileProviderErrorNoSuchItem userInfo:nil];
                sqlite3_reset(stmt);
                unlock(&mount->lock);
                return nil;
            }
            path = (const char *) sqlite3_column_text(stmt, 0);
            NSLog(@"trying to open %s for %lld", path, inode);
            int flag = O_RDONLY_;
            fd = mount->fs->open(mount, path, O_RDWR_, 0);
            if (PTR_ERR(fd) == _EISDIR)
                fd = mount->fs->open(mount, path, O_RDONLY_, 0);
            if (PTR_ERR(fd) == _ENOENT)
                goto step;
            sqlite3_reset(stmt);
            unlock(&mount->lock);
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
        NSLog(@"parent of %@ is %@", self.path, NSFileProviderRootContainerItemIdentifier);
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
        NSLog(@"uti of %@ is %@", self.path, (NSString *) kUTTypeDirectory);
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
        uti = @"public.text";
    NSLog(@"uti of %@ is %@", self.path, uti);
    return uti;
}

- (void)copyToURL:(NSURL *)url {
    NSLog(@"copying %@ to %@", self.path, url);
    lock(&self.fd->mount->lock);
    NSURL *itemURL = self.URL;
    NSError *err;
    [NSFileManager.defaultManager removeItemAtURL:itemURL error:nil];
    BOOL success = [NSFileManager.defaultManager copyItemAtURL:itemURL
                                                         toURL:url
                                                         error:&err];
    if (!success) {
        NSLog(@"error copying to %@: %@", url, err);
    }
    unlock(&self.fd->mount->lock);
}

- (void)copyFromURL:(NSURL *)url {
    NSLog(@"copying %@ from %@", self.path, url);
    NSInputStream *input = [NSInputStream inputStreamWithURL:url];
    [input open];
    uint8_t buf[1<<16];
    ssize_t n;
    do {
        n = [input read:buf maxLength:sizeof(buf)];
        if (n < 0) {
            NSLog(@"read failed because %@", input.streamError);
            abort();
        }
        n = self.fd->ops->write(self.fd, buf, n);
        [self handleError:n inFunction:@"write"];
    } while(n > 0);
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
