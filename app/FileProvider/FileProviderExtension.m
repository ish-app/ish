//
//  FileProviderExtension.m
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import "FileProviderExtension.h"
#import "FileProviderItem.h"
#import "FileProviderEnumerator.h"
#include "kernel/fs.h"

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
        NSURL *container = [manager containerURLForSecurityApplicationGroupIdentifier:@"group.app.ish.iSH"];
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
    NSURL *url = [NSFileProviderManager.defaultManager.documentStorageURL URLByAppendingPathComponent:identifier isDirectory:NO];
    NSLog(@"url for id %@ = %@", identifier, url);
    return url;
}

- (nullable NSFileProviderItemIdentifier)persistentIdentifierForItemAtURL:(NSURL *)url {
    NSString *identifier = url.lastPathComponent;
    NSLog(@"id for url %@ = %@", url, identifier);
    return identifier;
}

- (void)providePlaceholderAtURL:(NSURL *)url completionHandler:(void (^)(NSError * _Nullable error))completionHandler {
    NSError *err;
    FileProviderItem *item = [self itemForIdentifier:[self persistentIdentifierForItemAtURL:url] error:&err];
    if (item == nil) {
        completionHandler(err);
        return;
    }
    BOOL res = [NSFileProviderManager writePlaceholderAtURL:[NSFileProviderManager placeholderURLForURL:url]
                                               withMetadata:item
                                                      error:&err];
    if (!res) {
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
    NSFileManager *manager = NSFileManager.defaultManager;
    if (![manager fileExistsAtPath:url.path]) {
        [item loadToURL:url];
    }
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

#pragma mark - Actions

/* TODO: implement the actions for items here
 each of the actions follows the same pattern:
 - make a note of the change in the local model
 - schedule a server request as a background task to inform the server of the change
 - call the completion block with the modified item in its post-modification state
 */

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

