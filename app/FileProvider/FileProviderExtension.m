//
//  FileProviderExtension.m
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import "FileProviderExtension.h"
#import "FileProviderItem.h"
#import "FileProviderEnumerator.h"
#include "kernel/init.h"
#include "kernel/task.h"
#include "kernel/fs.h"

struct task *fake_task;

@interface FileProviderExtension ()
@property NSURL *rootData;
@end

@implementation FileProviderExtension

- (instancetype)init {
    if (self = [super init]) {
        NSFileManager *manager = NSFileManager.defaultManager;
        NSURL *container = [manager containerURLForSecurityApplicationGroupIdentifier:@"group.app.ish.iSH"];
        _rootData = [container URLByAppendingPathComponent:@"roots/alpine/data"];
        int err = mount_root(&fakefs, self.rootData.fileSystemRepresentation);
        if (err < 0) {
            NSLog(@"error opening root: %d", err);
        }
        // a couple things need to exist for the filesystem stuff to not segfault
        fake_task = task_create_(NULL);
        fake_task->fs = fs_info_new();
        current = fake_task;
    }
    return self;
}

- (nullable NSFileProviderItem)itemForIdentifier:(NSFileProviderItemIdentifier)identifier error:(NSError * _Nullable *)error {
    NSLog(@"item for id %@", identifier);
    return [[FileProviderItem alloc] initWithIdentifier:identifier];
}

- (nullable NSURL *)URLForItemWithPersistentIdentifier:(NSFileProviderItemIdentifier)identifier {
    NSURL *url = [self.rootData URLByAppendingPathComponent:identifier isDirectory:NO];
    NSLog(@"url for id %@ = %@", identifier, url);
    return url;
}

- (nullable NSFileProviderItemIdentifier)persistentIdentifierForItemAtURL:(NSURL *)url {
    NSUInteger pathStart = _rootData.pathComponents.count;
    NSUInteger pathLength = url.pathComponents.count - pathStart;
    NSArray<NSString *> *pathComponents = [url.pathComponents subarrayWithRange:NSMakeRange(pathStart, pathLength)];
    NSString *identifier = [NSString pathWithComponents:[@[@"/"] arrayByAddingObjectsFromArray:pathComponents]];
    NSLog(@"id for url %@ = %@", url, identifier);
    return identifier;
}

- (void)providePlaceholderAtURL:(NSURL *)url completionHandler:(void (^)(NSError * _Nullable error))completionHandler {
    completionHandler([NSError errorWithDomain:NSCocoaErrorDomain code:NSFeatureUnsupportedError userInfo:@{}]);
}

- (void)startProvidingItemAtURL:(NSURL *)url completionHandler:(void (^)(NSError *))completionHandler {
    // Should ensure that the actual file is in the position returned by URLForItemWithIdentifier:, then call the completion handler
    
    /* TODO:
     This is one of the main entry points of the file provider. We need to check whether the file already exists on disk,
     whether we know of a more recent version of the file, and implement a policy for these cases. Pseudocode:
     
     if (!fileOnDisk) {
         downloadRemoteFile();
         callCompletion(downloadErrorOrNil);
     } else if (fileIsCurrent) {
         callCompletion(nil);
     } else {
         if (localFileHasChanges) {
             // in this case, a version of the file is on disk, but we know of a more recent version
             // we need to implement a strategy to resolve this conflict
             moveLocalFileAside();
             scheduleUploadOfLocalFile();
             downloadRemoteFile();
             callCompletion(downloadErrorOrNil);
         } else {
             downloadRemoteFile();
             callCompletion(downloadErrorOrNil);
         }
     }
     */
    
    completionHandler([NSError errorWithDomain:NSCocoaErrorDomain code:NSFeatureUnsupportedError userInfo:@{}]);
}


- (void)itemChangedAtURL:(NSURL *)url {
    // Called at some point after the file has changed; the provider may then trigger an upload
    
    /* TODO:
     - mark file at <url> as needing an update in the model
     - if there are existing NSURLSessionTasks uploading this file, cancel them
     - create a fresh background NSURLSessionTask and schedule it to upload the current modifications
     - register the NSURLSessionTask with NSFileProviderManager to provide progress updates
     */
}

- (void)stopProvidingItemAtURL:(NSURL *)url {
    // Called after the last claim to the file has been released. At this point, it is safe for the file provider to remove the content file.
    return;
    
    // TODO: look up whether the file has local changes
    BOOL fileHasLocalChanges = NO;
    
    if (!fileHasLocalChanges) {
        // remove the existing file to free up space
        [[NSFileManager defaultManager] removeItemAtURL:url error:NULL];
        
        // write out a placeholder to facilitate future property lookups
        [self providePlaceholderAtURL:url completionHandler:^(NSError * __nullable error) {
            // TODO: handle any error, do any necessary cleanup
        }];
    }
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
    return [[FileProviderEnumerator alloc] initWithEnumeratedItemIdentifier:containerItemIdentifier];
}

@end

