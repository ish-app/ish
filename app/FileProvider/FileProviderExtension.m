//
//  FileProviderExtension.m
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import "FileProviderExtension.h"
#import "FileProviderItem.h"
#import "FileProviderEnumerator.h"
#import "NSError+ISHErrno.h"
#include "kernel/fs.h"
#import "../AppGroup.h"
#define ISH_INTERNAL
#include "fs/fake.h"

struct task *fake_task;

@interface FileProviderExtension () {
    struct mount *_mount;
};
@property NSURL *root;
@end

@implementation FileProviderExtension

- (BOOL)getMount:(struct mount **)mount error:(NSError **)error {
    @synchronized (self) {
        if (_mount == NULL) {
            if (self.domain == nil) {
                *error = [NSError errorWithDomain:NSFileProviderErrorDomain code:NSFileProviderErrorServerUnreachable userInfo:nil];
                return NO;
            }
            _mount = malloc(sizeof(struct mount));
            if (_mount == NULL) {
                *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:ENOMEM userInfo:nil];
                return NO;
            }
            _mount->fs = &fakefs;
            NSURL *container = ContainerURL();
            _root = [[[container URLByAppendingPathComponent:@"roots"]
                      URLByAppendingPathComponent:self.domain.identifier]
                     URLByAppendingPathComponent:@"data"];
            _mount->source = strdup(self.root.fileSystemRepresentation);
            int err = _mount->fs->mount(_mount);
            if (err < 0) {
                NSLog(@"error opening root: %d", err);
                *error = [NSError errorWithISHErrno:err itemIdentifier:NSFileProviderRootContainerItemIdentifier];
                free((void *) _mount->source);
                free(_mount);
                _mount = NULL;
                return NO;
            }
        }
        NSAssert(_mount != NULL, @"mount initialization should have succeeded");
        *mount = _mount;
        
        // Run a cleanup every once in a while. The idea here is that this
        // function gets called while the file provider is being interacted
        // with, so this should generally get time to run at that point, but we
        // don't want to do this when the user is not interacting with the file
        // provider.
        NSDate *lastCleanup = [NSUserDefaults.standardUserDefaults objectForKey:@"LastCleanup"];
        lastCleanup = lastCleanup ? lastCleanup : NSDate.distantPast;
        if ([lastCleanup timeIntervalSinceDate:NSDate.date] > 60 * 60 /* 1 hour */) {
            [self cleanupStorage];
        }
        [NSUserDefaults.standardUserDefaults setObject:NSDate.date forKey:@"LastCleanup"];
        
        return YES;
    }
}

- (NSURL *)storageURL {
    NSURL *storage = NSFileProviderManager.defaultManager.documentStorageURL;
    if (self.domain != nil)
        storage = [storage URLByAppendingPathComponent:self.domain.pathRelativeToDocumentStorage isDirectory:YES];
    return storage;
}

- (nullable NSFileProviderItem)itemForIdentifier:(NSFileProviderItemIdentifier)identifier error:(NSError * _Nullable *)error {
    struct mount *mount;
    if (![self getMount:&mount error:error]) return nil;
    NSLog(@"item for id %@", identifier);
    NSError *err;
    FileProviderItem *item = [[FileProviderItem alloc] initWithIdentifier:identifier mount:mount error:&err];
    if (item == nil) {
        if (error != nil)
            *error = err;
        return nil;
    }
    return item;
}

- (nullable NSURL *)URLForItemWithPersistentIdentifier:(NSFileProviderItemIdentifier)identifier {
    if ([identifier isEqualToString:NSFileProviderRootContainerItemIdentifier])
        return self.storageURL;
    FileProviderItem *item = [self itemForIdentifier:identifier error:nil];
    if (item == nil)
        return nil;
    NSURL *url = [self.storageURL URLByAppendingPathComponent:identifier isDirectory:YES];
    url = [url URLByAppendingPathComponent:item.path.lastPathComponent isDirectory:NO];
    NSLog(@"url for id %@ = %@", identifier, url);
    return url;
}

- (nullable NSFileProviderItemIdentifier)persistentIdentifierForItemAtURL:(NSURL *)url {
    if ([url.URLByDeletingLastPathComponent isEqual:NSFileProviderManager.defaultManager.documentStorageURL]) {
        NSAssert([self.domain.identifier isEqualToString:url.lastPathComponent], @"url isn't the same as our domain");
        return NSFileProviderRootContainerItemIdentifier;
    }
    NSString *identifier = url.pathComponents[url.pathComponents.count - 2];
    if (identifier.longLongValue == 0)
        return nil; // something must be screwed I guess
    NSLog(@"id for url %@ = %@", url, identifier);
    return identifier;
}

- (BOOL)enhanceSanityOfURL:(NSURL *)url error:(NSError **)error {
    NSURL *dir = url.URLByDeletingLastPathComponent;
    NSFileManager *manager = NSFileManager.defaultManager;
    BOOL isDir;
    if ([manager fileExistsAtPath:dir.path isDirectory:&isDir] && !isDir)
        [manager removeItemAtURL:dir error:nil];
    return [manager createDirectoryAtURL:dir
             withIntermediateDirectories:YES
                              attributes:nil
                                   error:error];
}

- (void)providePlaceholderAtURL:(NSURL *)url completionHandler:(void (^)(NSError * _Nullable error))completionHandler {
    NSError *err;
    FileProviderItem *item = [self itemForIdentifier:[self persistentIdentifierForItemAtURL:url] error:&err];
    if (item == nil) {
        completionHandler(err);
        return;
    }
    if (![self enhanceSanityOfURL:url error:&err]) {
        completionHandler(err);
        return;
    }
    if (![NSFileProviderManager writePlaceholderAtURL:[NSFileProviderManager placeholderURLForURL:url]
                                         withMetadata:item
                                                error:&err]) {
        completionHandler(err);
        return;
    }
    completionHandler(nil);
}

- (void)startProvidingItemAtURL:(NSURL *)url completionHandler:(void (^)(NSError *))completionHandler {
    // Should ensure that the actual file is in the position returned by URLForItemWithIdentifier:, then call the completion handler
    NSError *err;
    FileProviderItem *item = [self itemForIdentifier:[self persistentIdentifierForItemAtURL:url] error:&err];
    if (item == nil) {
        completionHandler(err);
        return;
    }
    if (![self enhanceSanityOfURL:url error:&err]) {
        completionHandler(err);
        return;
    }
    [item loadToURL:url];
    completionHandler(nil);
}

- (void)itemChangedAtURL:(NSURL *)url {
    FileProviderItem *item = [self itemForIdentifier:[self persistentIdentifierForItemAtURL:url] error:nil];
    if (item == nil)
        return;
    [item saveFromURL:url];
}

#pragma mark - Action helpers

// FIXME: not dry enough
// It's ok to use _mount in these because in each case the caller has already invoked itemForIdentifier:error: at least once
- (BOOL)doCreateDirectoryAt:(NSString *)path inode:(ino_t *)inode error:(NSError **)error {
    NSURL *url = [[NSURL fileURLWithPath:[NSString stringWithUTF8String:_mount->source]] URLByAppendingPathComponent:path];
    db_begin(&_mount->fakefs);
    if (![NSFileManager.defaultManager createDirectoryAtURL:url
                                withIntermediateDirectories:NO
                                                 attributes:@{NSFilePosixPermissions: @0777}
                                                      error:error]) {
        db_rollback(&_mount->fakefs);
        return nil;
    }
    struct ish_stat stat;
    NSString *parentPath = [path substringToIndex:[path rangeOfString:@"/" options:NSBackwardsSearch].location];
    if (!path_read_stat(&_mount->fakefs, parentPath.fileSystemRepresentation, &stat, NULL)) {
        db_rollback(&_mount->fakefs);
        *error = [NSError errorWithDomain:NSFileProviderErrorDomain code:NSFileProviderErrorNoSuchItem userInfo:nil];
        return nil;
    }
    stat.mode = (stat.mode & ~S_IFMT) | S_IFDIR;
    path_create(&_mount->fakefs, path.fileSystemRepresentation, &stat);
    if (inode != NULL)
        *inode = path_get_inode(&_mount->fakefs, path.fileSystemRepresentation);
    db_commit(&_mount->fakefs);
    return YES;
}

- (BOOL)doCreateFileAt:(NSString *)path importFrom:(NSURL *)importURL inode:(ino_t *)inode error:(NSError **)error {
    NSURL *url = [[NSURL fileURLWithPath:[NSString stringWithUTF8String:_mount->source]] URLByAppendingPathComponent:path];
    db_begin(&_mount->fakefs);
    if (![NSFileManager.defaultManager copyItemAtURL:importURL
                                               toURL:url
                                               error:error]) {
        db_rollback(&_mount->fakefs);
        return nil;
    }
    struct ish_stat stat;
    NSString *parentPath = [path substringToIndex:[path rangeOfString:@"/" options:NSBackwardsSearch].location];
    if (!path_read_stat(&_mount->fakefs, parentPath.fileSystemRepresentation, &stat, NULL)) {
        db_rollback(&_mount->fakefs);
        *error = [NSError errorWithDomain:NSFileProviderErrorDomain code:NSFileProviderErrorNoSuchItem userInfo:nil];
        return nil;
    }
    stat.mode = (stat.mode & ~S_IFMT & ~0111) | S_IFREG;
    path_create(&_mount->fakefs, path.fileSystemRepresentation, &stat);
    if (inode != NULL)
        *inode = path_get_inode(&_mount->fakefs, path.fileSystemRepresentation);
    db_commit(&_mount->fakefs);
    return YES;
}

- (NSString *)pathOfItemWithIdentifier:(NSFileProviderItemIdentifier)identifier error:(NSError **)error {
    FileProviderItem *parent = [self itemForIdentifier:identifier error:error];
    if (parent == nil)
        return nil;
    return parent.path;
}

#pragma mark - Actions

/* TODO: implement the actions for items here
 each of the actions follows the same pattern:
 - make a note of the change in the local model
 - schedule a server request as a background task to inform the server of the change
 - call the completion block with the modified item in its post-modification state
 */

- (void)createDirectoryWithName:(NSString *)directoryName inParentItemIdentifier:(NSFileProviderItemIdentifier)parentItemIdentifier completionHandler:(void (^)(NSFileProviderItem _Nullable, NSError * _Nullable))completionHandler {
    NSError *error;
    NSString *parentPath = [self pathOfItemWithIdentifier:parentItemIdentifier error:&error];
    if (parentPath == nil) {
        completionHandler(nil, error);
        return;
    }
    ino_t inode;
    if (![self doCreateDirectoryAt:[parentPath stringByAppendingFormat:@"/%@", directoryName] inode:&inode error:&error]) {
        completionHandler(nil, error);
        return;
    }
    FileProviderItem *item = [self itemForIdentifier:[NSString stringWithFormat:@"%lu", (unsigned long) inode] error:&error];
    if (item == nil)
        completionHandler(nil, error);
    else
        completionHandler(item, nil);
}

- (void)importDocumentAtURL:(NSURL *)fileURL toParentItemIdentifier:(NSFileProviderItemIdentifier)parentItemIdentifier completionHandler:(void (^)(NSFileProviderItem _Nullable, NSError * _Nullable))completionHandler {
    NSError *error;
    NSString *parentPath = [self pathOfItemWithIdentifier:parentItemIdentifier error:&error];
    if (parentPath == nil) {
        completionHandler(nil, error);
        return;
    }
    
    [fileURL startAccessingSecurityScopedResource];
    BOOL isDir;
    assert([NSFileManager.defaultManager fileExistsAtPath:fileURL.path isDirectory:&isDir] && !isDir);
    ino_t inode;
    BOOL worked = [self doCreateFileAt:[parentPath stringByAppendingFormat:@"/%@", fileURL.lastPathComponent]
                            importFrom:fileURL
                                 inode:&inode
                                 error:&error];
    [fileURL stopAccessingSecurityScopedResource];
    if (!worked) {
        completionHandler(nil, error);
        return;
    }
    
    FileProviderItem *item = [self itemForIdentifier:[NSString stringWithFormat:@"%lu", (unsigned long) inode] error:&error];
    if (item == nil)
        completionHandler(nil, error);
    else
        completionHandler(item, nil);
}

- (NSString *)pathFromURL:(NSURL *)url {
    NSURL *root = [NSURL fileURLWithPath:[NSString stringWithUTF8String:_mount->source]];
    assert([url.path hasPrefix:root.path]);
    NSString *path = [url.path substringFromIndex:root.path.length];
    assert([path hasPrefix:@"/"]);
    if ([path hasSuffix:@"/"])
        path = [path substringToIndex:path.length - 1];
    return path;
}

- (BOOL)doDelete:(NSString *)path itemIdentifier:(NSFileProviderItemIdentifier)identifier error:(NSError **)error {
    NSURL *url = [[NSURL fileURLWithPath:[NSString stringWithUTF8String:_mount->source]] URLByAppendingPathComponent:path];
    NSDirectoryEnumerator<NSURL *> *enumerator = [NSFileManager.defaultManager enumeratorAtURL:url
                                                                    includingPropertiesForKeys:nil
                                                                                       options:NSDirectoryEnumerationSkipsSubdirectoryDescendants
                                                                                  errorHandler:nil];
    for (NSURL *suburl in enumerator) {
        if (![self doDelete:[self pathFromURL:suburl] itemIdentifier:identifier error:error])
            return NO;
    }
    int err = fakefs.unlink(_mount, path.fileSystemRepresentation);
    if (err < 0)
        err = fakefs.rmdir(_mount, path.fileSystemRepresentation);
    if (err < 0) {
        *error = [NSError errorWithISHErrno:err itemIdentifier:identifier];
        return NO;
    }
    return YES;
}

- (void)deleteItemWithIdentifier:(NSFileProviderItemIdentifier)itemIdentifier completionHandler:(void (^)(NSError * _Nullable))completionHandler {
    NSError *error;
    NSString *path = [self pathOfItemWithIdentifier:itemIdentifier error:&error];
    if (path == nil) {
        completionHandler(error);
        return;
    }
    if (![self doDelete:path itemIdentifier:itemIdentifier error:&error])
        completionHandler(error);
    else
        completionHandler(nil);
}

- (BOOL)doRename:(NSString *)src to:(NSString *)dst itemIdentifier:(NSFileProviderItemIdentifier)identifier error:(NSError **)error {
    int err = fakefs.rename(_mount, src.fileSystemRepresentation, dst.fileSystemRepresentation);
    if (err < 0) {
        *error = [NSError errorWithISHErrno:err itemIdentifier:identifier];
        return NO;
    }
    return YES;
}

- (void)renameItemWithIdentifier:(NSFileProviderItemIdentifier)itemIdentifier toName:(NSString *)itemName completionHandler:(void (^)(NSFileProviderItem _Nullable, NSError * _Nullable))completionHandler {
    NSError *error;
    FileProviderItem *item = [self itemForIdentifier:itemIdentifier error:&error];
    if (item == nil) {
        completionHandler(nil, error);
        return;
    }
    NSString *dstPath = [item.path.stringByDeletingLastPathComponent stringByAppendingPathComponent:itemName];
    if (![self doRename:item.path to:dstPath itemIdentifier:itemIdentifier error:&error]) {
        completionHandler(nil, error);
        return;
    }
    completionHandler(item, nil);
}

- (void)reparentItemWithIdentifier:(NSFileProviderItemIdentifier)itemIdentifier toParentItemWithIdentifier:(NSFileProviderItemIdentifier)parentItemIdentifier newName:(NSString *)newName completionHandler:(void (^)(NSFileProviderItem _Nullable, NSError * _Nullable))completionHandler {
    NSError *error;
    FileProviderItem *item = [self itemForIdentifier:itemIdentifier error:&error];
    if (item == nil) {
        completionHandler(nil, error);
        return;
    }
    FileProviderItem *parent = [self itemForIdentifier:parentItemIdentifier error:&error];
    if (parent == nil) {
        completionHandler(nil, error);
        return;
    }
    if (newName == nil)
        newName = item.path.lastPathComponent;
    if (![self doRename:item.path to:[parent.path stringByAppendingPathComponent:newName] itemIdentifier:itemIdentifier error:&error]) {
        completionHandler(nil, error);
        return;
    }
    completionHandler(item, nil);
}

#pragma mark - Enumeration

- (nullable id<NSFileProviderEnumerator>)enumeratorForContainerItemIdentifier:(NSFileProviderItemIdentifier)containerItemIdentifier error:(NSError **)error {
    FileProviderItem *item = [self itemForIdentifier:containerItemIdentifier error:error];
    if (item == nil)
        return nil;
    return [[FileProviderEnumerator alloc] initWithItem:item];
}

- (void)dealloc {
    if (_mount) {
        _mount->fs->umount(_mount);
        free(_mount);
    }
}

#pragma mark - Storage deletion

// According to an engineer I talked to at WWDC, -stopProvidingItemAtURL: is never ever called, so that can't be used to free up disk space.
// Solution for now is to periodically look for and delete files in file provider storage where the original is missing.
// TODO: Delete files in file provider storage when the original file is deleted
// TODO: Create hardlinks into file provider storage instead of copies
//
- (void)cleanupStorage {
    NSAssert(_mount, @"Mount should exist by this point");

    NSFileManager *manager = NSFileManager.defaultManager;
    NSArray<NSURL *> *storageDirs = [manager contentsOfDirectoryAtURL:self.storageURL includingPropertiesForKeys:nil options:0 error:nil];
    for (NSURL *dir in storageDirs) {
        ino_t inode = dir.lastPathComponent.longLongValue;
        if (inode == 0)
            continue;
        struct fd *fd = fakefs_open_inode(_mount, inode);
        if (IS_ERR(fd)) {
            NSLog(@"removing dead inode %llu", inode);
            NSError *err;
            if (![manager removeItemAtURL:dir error:&err])
                NSLog(@"failed to remove dead inode: %@", err);
        } else {
            fd_close(fd);
        }
    }
}

// Dead code, leaving it here just in case
- (void)stopProvidingItemAtURL:(NSURL *)url {
    FileProviderItem *item = [self itemForIdentifier:[self persistentIdentifierForItemAtURL:url] error:nil];
    if (item == nil)
        return;
    [item saveFromURL:url];
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
    [NSFileProviderManager writePlaceholderAtURL:[NSFileProviderManager placeholderURLForURL:url]
                                    withMetadata:item
                                           error:nil];
}

@end
