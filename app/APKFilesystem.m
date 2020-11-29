//
//  APKFilesystem.m
//  iSH
//
//  Created by Theodore Dubois on 11/27/20.
//

#import <Foundation/Foundation.h>
#import "APKFilesystem.h"
#include "kernel/errno.h"
#include "kernel/fs.h"
#include "fs/real.h"

static NSSet<NSString *> *ODRTags() {
    static NSMutableSet<NSString *> *tags;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSDictionary *plist = [NSDictionary dictionaryWithContentsOfURL:[NSBundle.mainBundle URLForResource:@"OnDemandResources" withExtension:@"plist"]];
        NSDictionary *tagsDict = plist[@"NSBundleResourceRequestTags"];
        tags = [NSMutableSet new];
        for (NSString *key in tagsDict.keyEnumerator) {
            [tags addObject:key];
            // Add "directories", e.g. main:x86:APKINDEX.tar.gz also adds main: and main:x86:
            for (NSUInteger i = 0; i < key.length; i++) {
                if ([key characterAtIndex:i] == '/') {
                    [tags addObject:[key substringToIndex:i+1]];
                }
            }
        }
        [tags addObject:@":"];
    });
    return tags;
}

static NSString *TagForPath(const char *path) {
    NSString *tag = [[NSString stringWithCString:path encoding:NSUTF8StringEncoding]
                     stringByReplacingOccurrencesOfString:@"/" withString:@":"];
    if ([tag hasPrefix:@":"])
        tag = [tag substringFromIndex:1];
    return tag;
}

static int apkfs_path_type(const char *path) {
    NSSet<NSString *> *odrTags = ODRTags();
    NSString *tag = TagForPath(path);
    if ([odrTags containsObject:tag])
        return S_IFREG;
    else if ([odrTags containsObject:[tag stringByAppendingString:@":"]])
        return S_IFDIR;
    else
        return 0;
}

static const struct fd_ops apkfs_dir_ops;
static const struct fd_ops apkfs_file_ops;

static int apkfs_stat(struct mount *mount, const char *path, struct statbuf *stat) {
    switch (apkfs_path_type(path)) {
        case S_IFREG:
            stat->mode = S_IFREG | 0444; break;
        case S_IFDIR:
            stat->mode = S_IFDIR | 0555; break;
        default:
            return _ENOENT;
    }
    return 0;
}
static int apkfs_fstat(struct fd *fd, struct statbuf *stat) {
    if (fd->ops == &apkfs_dir_ops)
        stat->mode = S_IFDIR | 0555;
    else if (fd->ops == &apkfs_file_ops)
        stat->mode = S_IFREG | 0444;
    return 0;
}

// The original plan was to declare a __block lock_t complete_lock, but it turns out capturing a __block var moves it from the stack to the heap, and locks can't be moved.
@interface DownloadCompletion : NSObject {
    @public
    BOOL complete;
    NSError *error;
    lock_t complete_lock;
    cond_t complete_cond;
}
@end
@implementation DownloadCompletion
@end

static struct fd *apkfs_open(struct mount *mount, const char *path, int flags, int mode) {
    int type = apkfs_path_type(path);
    if (type == 0)
        return ERR_PTR(_ENOENT);
    if (type == S_IFDIR)
        return fd_create(&apkfs_dir_ops);

    if (type == S_IFREG) {
        DownloadCompletion *c = [DownloadCompletion new];
        lock_init(&c->complete_lock);
        cond_init(&c->complete_cond);
        __weak DownloadCompletion *wc = c;

        NSString *tag = TagForPath(path);
        NSBundleResourceRequest *request = [[NSBundleResourceRequest alloc] initWithTags:[NSSet setWithObject:tag]];
        request.loadingPriority = NSBundleResourceRequestLoadingPriorityUrgent;
        __weak NSBundleResourceRequest *weakRequest = request;
        [NSNotificationCenter.defaultCenter postNotificationName:APKDownloadStartedNotification object:request userInfo:nil];
        [request beginAccessingResourcesWithCompletionHandler:^(NSError * _Nullable err) {
            [NSNotificationCenter.defaultCenter postNotificationName:APKDownloadFinishedNotification object:weakRequest userInfo:nil];
            DownloadCompletion *c = wc;
            if (c) {
                lock(&c->complete_lock);
                c->error = err;
                c->complete = YES;
                notify(&c->complete_cond);
                unlock(&c->complete_lock);
            }
        }];

        while (!c->complete) {
            int err = wait_for(&c->complete_cond, &c->complete_lock, NULL);
            if (err == _EINTR) {
                [request.progress cancel];
                return ERR_PTR(err);
            }
        }

        NSError *error = c->error;
        if (error != nil) {
            int err = _EIO;
            if ([error.domain isEqualToString:NSCocoaErrorDomain]) {
                if (error.code == NSBundleOnDemandResourceInvalidTagError)
                    err = _ENOENT;
                if (error.code == NSBundleOnDemandResourceOutOfSpaceError)
                    err = _ENOSPC;
                if (error.code == NSUserCancelledError)
                    err = _ECANCELED;
            }
            return ERR_PTR(err);
        }

        struct fd *fd = fd_create(&apkfs_file_ops);
        if (fd == NULL)
            return ERR_PTR(_ENOMEM);
        fd->real_fd = open([request.bundle URLForResource:tag withExtension:nil].fileSystemRepresentation, O_RDONLY);
        if (fd->real_fd < 0)
            return ERR_PTR(errno_map());
        fd->data = (void *) CFBridgingRetain(request);
        return fd;
    }
    return ERR_PTR(_ENOENT);
}

static int apkfs_close(struct fd *fd) {
    CFBridgingRelease(fd->data);
    return 0;
}

static int apkfs_getpath(struct fd *fd, char *buf) {
    if (fd->ops == &apkfs_dir_ops) {
        strcpy(buf, "");
    } else if (fd->ops == &apkfs_file_ops) {
        NSBundleResourceRequest *req = (__bridge NSBundleResourceRequest *) fd->data;
        strcpy(buf, [@"/" stringByAppendingString:[req.tags.anyObject stringByReplacingOccurrencesOfString:@":" withString:@"/"]].UTF8String);
    }
    return 0;
}

static int apkfs_dir_readdir(struct fd *fd, struct dir_entry *entry) {
    return 0;
}

const struct fs_ops apkfs = {
    .name = "apk", .magic = 0x61706b20,
    .stat = apkfs_stat,
    .open = apkfs_open,
    .fstat = apkfs_fstat,
    .close = apkfs_close,
    .getpath = apkfs_getpath,
};

static const struct fd_ops apkfs_dir_ops = {
    .readdir = apkfs_dir_readdir,
};
static const struct fd_ops apkfs_file_ops = {
    .read = realfs_read,
    .lseek = realfs_lseek,
    .close = realfs_close,
};

NSString *const APKDownloadStartedNotification = @"APKDownloadStartedNotification";
NSString *const APKDownloadFinishedNotification = @"APKDownloadFinishedNotification";
