//
//  FileProviderItem.m
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import <MobileCoreServices/MobileCoreServices.h>
#include <sys/stat.h>
#include <dirent.h>
#import "FileProviderExtension.h"
#import "FileProviderItem.h"
#include "fs/fake-db.h"
#include "kernel/errno.h"

@interface FileProviderItem ()

@property (readonly) NSFileProviderItemIdentifier identifier;
@property (readonly) int fd;
@property (readonly) BOOL isRoot;

@end

@implementation FileProviderItem

- (instancetype)initWithIdentifier:(NSFileProviderItemIdentifier)identifier mount:(struct fakefs_mount *)mount error:(NSError *__autoreleasing  _Nullable *)error {
    if (self = [super init]) {
        _identifier = identifier;
        _mount = mount;
        _fd = [self openNewFDWithError:error];
        if (_fd == -1)
            return nil;
    }
    return self;
}

- (BOOL)isRoot {
    return [self.identifier isEqualToString:NSFileProviderRootContainerItemIdentifier];
}

- (int)openNewFDWithError:(NSError *__autoreleasing  _Nullable *)error {
    int fd = -1;
    if (self.isRoot) {
        fd = open(_mount->source, O_DIRECTORY | O_RDONLY);
    } else {
        db_begin(&_mount->db);
        sqlite3_stmt *stmt = _mount->db.stmt.path_from_inode;
        sqlite3_bind_int64(_mount->db.stmt.path_from_inode, 1, _identifier.longLongValue);
        while (db_exec(&_mount->db, stmt)) {
            const char *path = (const char *) sqlite3_column_text(stmt, 0);
            fd = openat(_mount->root_fd, fix_path(path), O_RDWR);
            if (fd == -1 && errno == EISDIR)
                fd = openat(_mount->root_fd, fix_path(path), O_RDONLY);
            if (fd == -1 && errno != ENOENT)
                break;
        }
        db_reset(&_mount->db, stmt);
        db_commit(&_mount->db);
    }
    if (fd == -1) {
        if (error != nil) {
            if (errno == ENOENT)
                *error = [NSError fileProviderErrorForNonExistentItemWithIdentifier:_identifier];
            else
                *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:nil];
        }
        NSLog(@"opening %@ failed: %@", self.identifier, *error);
        return -1;
    }
    return fd;
}

- (NSString *)path {
    char path[PATH_MAX] = "";
    int err = fcntl(_fd, F_GETPATH, path);
    [self handleError:err inFunction:@"getpath"];
    return [NSString stringWithUTF8String:path + strlen(_mount->source)];
}

- (NSURL *)URL {
    NSURL *rootURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:_mount->source]];
    if (self.isRoot)
        return rootURL;
    return [rootURL URLByAppendingPathComponent:self.path];
}

- (struct ish_stat)ishStat {
    struct ish_stat stat = {};
    db_begin(&_mount->db);
    inode_t inode = _identifier.longLongValue;
    if ([_identifier isEqualToString:NSFileProviderRootContainerItemIdentifier])
        inode = path_get_inode(&_mount->db, "");
    inode_read_stat(&_mount->db, inode, &stat);
    db_commit(&_mount->db);
    return stat;
}
- (struct stat)realStat {
    struct stat statbuf;
    int err = fstat(_fd, &statbuf);
    [self handleError:err inFunction:@"realStat"];
    return statbuf;
}

- (NSFileProviderItemIdentifier)itemIdentifier {
    if (self.isRoot)
        return NSFileProviderRootContainerItemIdentifier;
    return _identifier;
}
- (NSFileProviderItemIdentifier)parentItemIdentifier {
    if (self.isRoot) {
        NSLog(@"parent of root %@ is %@", self.path, NSFileProviderRootContainerItemIdentifier);
        return NSFileProviderRootContainerItemIdentifier;
    }
    NSString *parentPath = self.path.stringByDeletingLastPathComponent;
    if ([parentPath isEqualToString:@"/"])
        return NSFileProviderRootContainerItemIdentifier;
    db_begin(&_mount->db);
    inode_t parentInode = path_get_inode(&_mount->db, parentPath.UTF8String);
    db_commit(&_mount->db);
    assert(parentInode != 0);
    NSString *parent = [NSString stringWithFormat:@"%lu", (unsigned long) parentInode];
    NSLog(@"parent of %@ is %@", self.path, parent);
    return parent;
}

- (NSFileProviderItemCapabilities)capabilities {
    NSFileProviderItemCapabilities caps = NSFileProviderItemCapabilitiesAllowsDeleting | NSFileProviderItemCapabilitiesAllowsRenaming | NSFileProviderItemCapabilitiesAllowsReparenting;
    if (S_ISREG(self.ishStat.mode))
        caps |= NSFileProviderItemCapabilitiesAllowsReading | NSFileProviderItemCapabilitiesAllowsWriting;
    else if (S_ISDIR(self.ishStat.mode))
        caps |= NSFileProviderItemCapabilitiesAllowsAddingSubItems | NSFileProviderItemCapabilitiesAllowsContentEnumerating;
    else
        return 0;
    return caps;
}

- (NSString *)filename {
    NSString *filename = self.path.lastPathComponent;
    if ([filename isEqualToString:@""])
        filename = @"/";
    NSLog(@"filename %@", filename);
    return filename;
}

- (NSNumber *)documentSize {
    struct stat statbuf;
    int err = fstat(_fd, &statbuf);
    [self handleError:err inFunction:@"documentSize"];
    return [NSNumber numberWithUnsignedLongLong:statbuf.st_size];
}

- (NSNumber *)childItemCount {
    if (!S_ISDIR(self.ishStat.mode))
        return @0;
    int fd = [self openNewFDWithError:nil];
    if (fd == -1)
        return @0;
    unsigned n = 0;
    DIR *dir = fdopendir(fd);
    struct dirent *dirent;
    while ((dirent = readdir(dir))) {
        if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
            continue;
        n++;
    }
    closedir(dir);
    return @(n);
}

- (NSDate *)contentModificationDate {
    struct stat statbuf = self.realStat;
    return [NSDate dateWithTimeIntervalSince1970:statbuf.st_mtimespec.tv_sec + (NSTimeInterval) statbuf.st_mtimespec.tv_nsec / 1000000000];
}

- (NSString *)typeIdentifier {
    if (self.isRoot) {
        NSLog(@"uti of %@ is %@", self.path, (NSString *) kUTTypeFolder);
        return (NSString *) kUTTypeFolder;
    }
    mode_t_ mode = self.ishStat.mode;
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
    sqlite3_mutex_enter(_mount->db.lock);
    [NSFileManager.defaultManager removeItemAtURL:url error:nil];
    BOOL success = [NSFileManager.defaultManager copyItemAtURL:itemURL
                                                         toURL:url
                                                         error:&err];
    sqlite3_mutex_leave(_mount->db.lock);
    if (!success) {
        NSLog(@"error copying to %@: %@", url, err);
    }
}

- (void)saveFromURL:(NSURL *)url {
    NSLog(@"copying %@ from %@", self.path, url);
    NSURL *itemURL = self.URL;
    NSError *err;
    sqlite3_mutex_enter(_mount->db.lock);
    [NSFileManager.defaultManager removeItemAtURL:itemURL error:nil];
    BOOL success = [NSFileManager.defaultManager copyItemAtURL:url
                                                         toURL:itemURL
                                                         error:&err];
    sqlite3_mutex_leave(_mount->db.lock);
    if (!success) {
        NSLog(@"error copying to %@: %@", url, err);
    }
}

- (void)dealloc {
    if (self.fd != -1)
        close(self.fd);
}

- (void)handleError:(long)err inFunction:(NSString *)func {
    if (err < 0) {
        NSLog(@"%@ returned %ld %d", func, err, errno);
        abort();
    }
}

@end
