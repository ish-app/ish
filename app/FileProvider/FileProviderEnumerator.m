//
//  FileProviderEnumerator.m
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import <MobileCoreServices/MobileCoreServices.h>
#import "FileProviderEnumerator.h"
#import "FileProviderExtension.h"
#import "FileProviderItem.h"
#import "NSError+ISHErrno.h"
#include "kernel/fs.h"

@interface FileProviderEnumerator ()

@property FileProviderItem *item;

@end

@implementation FileProviderEnumerator

- (instancetype)initWithItem:(FileProviderItem *)item {
    if (self = [super init]) {
        self.item = item;
    }
    return self;
}

- (void)enumerateItemsForObserver:(id<NSFileProviderEnumerationObserver>)observer startingAtPage:(NSFileProviderPage)page {
    NSLog(@"enumeration start %@", self.item.itemIdentifier);
    // if we're asked to enumerate the working set
    if (self.item == nil) {
        [observer finishEnumeratingUpToPage:page];
        return;
    }
    // if we're asked to enumerate a file
    if (![self.item.typeIdentifier isEqualToString:(NSString *) kUTTypeFolder]) {
        NSLog(@"not enumerating a file (%@)", self.item.typeIdentifier);
        [observer finishEnumeratingUpToPage:page];
        return;
    }
    
    NSError *error;
    struct fd *fd = [self.item openNewFDWithError:&error];
    if (fd == NULL) {
        [observer finishEnumeratingWithError:error];
        return;
    }
    NSMutableArray<FileProviderItem *> *items = [NSMutableArray new];
    while (true) {
        struct dir_entry dirent = {};
        int err = fd->ops->readdir(fd, &dirent);
        if (err < 0) {
            NSLog(@"readdir returned %d", err);
            [observer finishEnumeratingWithError:[NSError errorWithISHErrno:err itemIdentifier:self.item.itemIdentifier]];
            fd_close(fd);
            return;
        }
        if (err == 0)
            break;
        if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0)
            continue;
        NSString *childIdent = [NSString stringWithFormat:@"%lu", (unsigned long) dirent.inode];
        NSLog(@"returning %s %@", dirent.name, childIdent);
        FileProviderItem *item = [[FileProviderItem alloc] initWithIdentifier:childIdent mount:fd->mount error:&error];
        if (item == nil) {
            [observer finishEnumeratingWithError:error];
            fd_close(fd);
            return;
        }
        [items addObject:item];
    }
    fd_close(fd);
    NSLog(@"returning %@", items);
    [observer didEnumerateItems:items];
    [observer finishEnumeratingUpToPage:nil];
}

- (void)enumerateChangesForObserver:(id<NSFileProviderChangeObserver>)observer fromSyncAnchor:(NSFileProviderSyncAnchor)anchor {
    NSLog(@"saying no file changes");
    // TODO implement by having the sync anchor be a serialized list of files
    [observer finishEnumeratingChangesUpToSyncAnchor:anchor moreComing:NO];
}

- (void)invalidate {
}

@end
