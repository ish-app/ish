//
//  FileProviderEnumerator.m
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import <MobileCoreServices/MobileCoreServices.h>
#include <dirent.h>
#import "FileProviderExtension.h"
#import "FileProviderEnumerator.h"
#import "FileProviderItem.h"
#import "NSError+ISHErrno.h"
#include "fs/fake-db.h"

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
    int fd = [self.item openNewFDWithError:&error];
    if (fd == -1) {
        [observer finishEnumeratingWithError:error];
        return;
    }
    DIR *dir = fdopendir(fd);
    NSMutableArray<FileProviderItem *> *items = [NSMutableArray new];
    struct dirent *dirent;
    errno = 0;
    while ((dirent = readdir(dir))) {
        if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
            continue;

        // this is annoying
        NSString *path = _item.path;
        NSString *childIdent;
        if (strcmp(dirent->d_name, "..") == 0) {
            childIdent = _item.parentItemIdentifier;
        } else if (strcmp(dirent->d_name, ".") != 0) {
            db_begin(&_item.mount->db);
            inode_t inode = path_get_inode(&_item.mount->db, [path stringByAppendingFormat:@"/%@", [NSString stringWithUTF8String:dirent->d_name]].fileSystemRepresentation);
            NSAssert(inode != 0, @"");
            db_commit(&_item.mount->db);
            childIdent = [NSString stringWithFormat:@"%lu", (unsigned long) inode];
        }

        NSLog(@"returning %s %@", dirent->d_name, childIdent);
        FileProviderItem *item = [[FileProviderItem alloc] initWithIdentifier:childIdent mount:_item.mount error:&error];
        if (item == nil) {
            [observer finishEnumeratingWithError:error];
            closedir(dir);
            return;
        }
        [items addObject:item];
        errno = 0;
    }
    if (errno != 0) {
        NSError *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:nil];
        NSLog(@"readdir returned %@", error);
        [observer finishEnumeratingWithError:error];
        closedir(dir);
        return;
    }

    closedir(dir);
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
