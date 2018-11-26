//
//  FileProviderEnumerator.m
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import "FileProviderEnumerator.h"
#import "FileProviderExtension.h"
#import "FileProviderItem.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "kernel/errno.h"

@implementation FileProviderEnumerator

- (instancetype)initWithEnumeratedItemIdentifier:(NSFileProviderItemIdentifier)enumeratedItemIdentifier {
    if (self = [super init]) {
        _enumeratedItemIdentifier = enumeratedItemIdentifier;
    }
    return self;
}

- (void)invalidate {
    // TODO: perform invalidation of server connection if necessary
}

- (NSError *)errorFromCode:(long)code {
    if (code >= 0)
        return nil;
    if (code == _ENOENT)
        return [NSError fileProviderErrorForNonExistentItemWithIdentifier:self.enumeratedItemIdentifier];
    return [NSError errorWithDomain:@"iSHErrorDomain" code:code userInfo:@{NSLocalizedDescriptionKey: @"a bad thing happened"}];
}

- (void)enumerateItemsForObserver:(id<NSFileProviderEnumerationObserver>)observer startingAtPage:(NSFileProviderPage)page {
    static int i;
    i++;
    NSString *path = self.enumeratedItemIdentifier;
    if ([path isEqualToString:NSFileProviderWorkingSetContainerItemIdentifier]) {
        [observer finishEnumeratingUpToPage:page];
        return;
    }
    
    if ([path isEqualToString:NSFileProviderRootContainerItemIdentifier])
        path = @"/";
    NSLog(@"enumerating %@ %d", path, i);
    current = fake_task; // sometimes it isn't because threads are strange
    struct fd *fd = generic_open(path.UTF8String, O_RDONLY_, 0);
    if (IS_ERR(fd)) {
        NSLog(@"could not open %@", path);
        [observer finishEnumeratingWithError:[self errorFromCode:PTR_ERR(fd)]];
        return;
    }
    
    NSMutableArray<FileProviderItem *> *items = [NSMutableArray new];
    while (true) {
        struct dir_entry dirent;
        int err = fd->ops->readdir(fd, &dirent);
        if (err < 0) {
            NSLog(@"could not readdir %@", path);
            [observer finishEnumeratingWithError:[self errorFromCode:err]];
            return;
        }
        if (err == 0)
            break;
        if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0)
            continue;
        NSString *childPath = [path stringByAppendingPathComponent:[NSString stringWithUTF8String:dirent.name]];
        NSLog(@"returning %@", childPath);
        [items addObject:[[FileProviderItem alloc] initWithIdentifier:childPath]];
    }
    [observer didEnumerateItems:items];
    [observer finishEnumeratingUpToPage:nil];
    fd_close(fd);
}

- (void)enumerateChangesForObserver:(id<NSFileProviderChangeObserver>)observer fromSyncAnchor:(NSFileProviderSyncAnchor)anchor {
    // TODO implement by having the sync anchor be a serialized list of files
    [observer finishEnumeratingChangesUpToSyncAnchor:anchor moreComing:NO];
}

@end
