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
#define ISH_INTERNAL
#include "fs/fake.h"

struct task *fake_task;

@interface FileProviderExtension ()
@property NSURL *root;
@property struct mount *mount;
@end

@implementation FileProviderExtension

- (instancetype)init {
    if (self = [super init]) {
        self.mount = malloc(sizeof(struct mount));
        if (!self.mount)
            return nil;
        self.mount->fs = &fakefs;
        NSFileManager *manager = NSFileManager.defaultManager;
        NSURL *container = [manager containerURLForSecurityApplicationGroupIdentifier:PRODUCT_APP_GROUP_IDENTIFIER];
        _root = [container URLByAppendingPathComponent:@"roots/alpine/data"];
        self.mount->source = strdup(self.root.fileSystemRepresentation);
        int err = self.mount->fs->mount(self.mount);
        if (err < 0) {
            NSLog(@"error opening root: %d", err);
            return nil;
        }
    }
    return self;
}

- (nullable NSFileProviderItem)itemForIdentifier:(NSFileProviderItemIdentifier)identifier error:(NSError * _Nullable *)error {
    NSLog(@"item for id %@", identifier);
    NSError *err;
    FileProviderItem *item = [[FileProviderItem alloc] initWithIdentifier:identifier mount:self.mount error:&err];
    if (item == nil) {
        if (error != nil)
            *error = err;
        return nil;
    }
    return item;
}

- (nullable NSURL *)URLForItemWithPersistentIdentifier:(NSFileProviderItemIdentifier)identifier {
    FileProviderItem *item = [self itemForIdentifier:identifier error:nil];
    if (item == nil)
        return nil;
    NSURL *storage = NSFileProviderManager.defaultManager.documentStorageURL;
    NSURL *url = [storage URLByAppendingPathComponent:identifier isDirectory:YES];
    url = [url URLByAppendingPathComponent:item.path.lastPathComponent isDirectory:NO];
    NSLog(@"url for id %@ = %@", identifier, url);
    return url;
}

- (nullable NSFileProviderItemIdentifier)persistentIdentifierForItemAtURL:(NSURL *)url {
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

#pragma mark - Action helpers

// FIXME: not dry enough
- (BOOL)doCreateDirectoryAt:(NSString *)path inode:(ino_t *)inode error:(NSError **)error {
    NSURL *url = [[NSURL fileURLWithPath:[NSString stringWithUTF8String:self.mount->source]] URLByAppendingPathComponent:path];
    db_begin(self.mount);
    if (![NSFileManager.defaultManager createDirectoryAtURL:url
                                withIntermediateDirectories:NO
                                                 attributes:@{NSFilePosixPermissions: @0777}
                                                      error:error]) {
        db_rollback(self.mount);
        return nil;
    }
    struct ish_stat stat;
    NSString *parentPath = [path substringToIndex:[path rangeOfString:@"/" options:NSBackwardsSearch].location];
    if (!path_read_stat(self.mount, parentPath.fileSystemRepresentation, &stat, NULL)) {
        db_rollback(self.mount);
        *error = [NSError errorWithDomain:NSFileProviderErrorDomain code:NSFileProviderErrorNoSuchItem userInfo:nil];
        return nil;
    }
    stat.mode = (stat.mode & ~S_IFMT) | S_IFDIR;
    path_create(self.mount, path.fileSystemRepresentation, &stat);
    if (inode != NULL)
        *inode = path_get_inode(self.mount, path.fileSystemRepresentation);
    db_commit(self.mount);
    return YES;
}

- (BOOL)doCreateFileAt:(NSString *)path importFrom:(NSURL *)importURL inode:(ino_t *)inode error:(NSError **)error {
    NSURL *url = [[NSURL fileURLWithPath:[NSString stringWithUTF8String:self.mount->source]] URLByAppendingPathComponent:path];
    db_begin(self.mount);
    if (![NSFileManager.defaultManager copyItemAtURL:importURL
                                               toURL:url
                                               error:error]) {
        db_rollback(self.mount);
        return nil;
    }
    struct ish_stat stat;
    NSString *parentPath = [path substringToIndex:[path rangeOfString:@"/" options:NSBackwardsSearch].location];
    if (!path_read_stat(self.mount, parentPath.fileSystemRepresentation, &stat, NULL)) {
        db_rollback(self.mount);
        *error = [NSError errorWithDomain:NSFileProviderErrorDomain code:NSFileProviderErrorNoSuchItem userInfo:nil];
        return nil;
    }
    stat.mode = (stat.mode & ~S_IFMT & ~0111) | S_IFREG;
    path_create(self.mount, path.fileSystemRepresentation, &stat);
    if (inode != NULL)
        *inode = path_get_inode(self.mount, path.fileSystemRepresentation);
    db_commit(self.mount);
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
    NSURL *root = [NSURL fileURLWithPath:[NSString stringWithUTF8String:self.mount->source]];
    assert([url.path hasPrefix:root.path]);
    NSString *path = [url.path substringFromIndex:root.path.length];
    assert([path hasPrefix:@"/"]);
    if ([path hasSuffix:@"/"])
        path = [path substringToIndex:path.length - 1];
    return path;
}

- (BOOL)doDelete:(NSString *)path itemIdentifier:(NSFileProviderItemIdentifier)identifier error:(NSError **)error {
    NSURL *url = [[NSURL fileURLWithPath:[NSString stringWithUTF8String:self.mount->source]] URLByAppendingPathComponent:path];
    NSDirectoryEnumerator<NSURL *> *enumerator = [NSFileManager.defaultManager enumeratorAtURL:url
                                                                    includingPropertiesForKeys:nil
                                                                                       options:NSDirectoryEnumerationSkipsSubdirectoryDescendants
                                                                                  errorHandler:nil];
    for (NSURL *suburl in enumerator) {
        if (![self doDelete:[self pathFromURL:suburl] itemIdentifier:identifier error:error])
            return NO;
    }
    int err = fakefs.unlink(self.mount, path.fileSystemRepresentation);
    if (err < 0)
        err = fakefs.rmdir(self.mount, path.fileSystemRepresentation);
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
    int err = fakefs.rename(self.mount, src.fileSystemRepresentation, dst.fileSystemRepresentation);
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
    self.mount->fs->umount(self.mount);
    free(self.mount);
}

@end
