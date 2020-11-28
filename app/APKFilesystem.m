//
//  APKFilesystem.m
//  iSH
//
//  Created by Theodore Dubois on 11/27/20.
//

#import <Foundation/Foundation.h>
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

static struct fd *apkfs_open(struct mount *mount, const char *path, int flags, int mode) {
    int type = apkfs_path_type(path);
    if (type == 0)
        return ERR_PTR(_ENOENT);
    if (type == S_IFDIR)
        return fd_create(&apkfs_dir_ops);

    if (type == S_IFREG) {
        __block BOOL complete = NO;
        __block NSError *error;
        __block lock_t complete_lock;
        __block cond_t complete_cond;
        lock_init(&complete_lock);
        cond_init(&complete_cond);

        NSString *tag = TagForPath(path);
        NSBundleResourceRequest *request = [[NSBundleResourceRequest alloc] initWithTags:[NSSet setWithObject:tag]];
        [request beginAccessingResourcesWithCompletionHandler:^(NSError * _Nullable err) {
            lock(&complete_lock);
            error = err;
            complete = YES;
            notify(&complete_cond);
            unlock(&complete_lock);
        }];

        while (!complete) {
            int err = wait_for(&complete_cond, &complete_lock, NULL);
            if (err == _EINTR) {
                [request.progress cancel];
                [request endAccessingResources];
                return ERR_PTR(err);
            }
        }

        if (error != nil) {
            int err = _EIO;
            if (error.code == NSBundleOnDemandResourceInvalidTagError)
                err = _ENOENT;
            if (error.code == NSBundleOnDemandResourceOutOfSpaceError)
                err = _ENOSPC;
            return ERR_PTR(err);
        }

        struct fd *fd = fd_create(&apkfs_file_ops);
        if (fd == NULL) {
            [request endAccessingResources];
            return ERR_PTR(_ENOMEM);
        }
        const char *real_path = [request.bundle URLForResource:tag withExtension:nil].fileSystemRepresentation;
        fd->real_fd = open(real_path, O_RDONLY);
        if (fd->real_fd < 0) {
            [request endAccessingResources];
            return ERR_PTR(errno_map());
        }
        fd->data = (void *) CFBridgingRetain(request);
        return fd;
    }
    return ERR_PTR(_ENOENT);
}

static int apkfs_close(struct fd *fd) {
    NSBundleResourceRequest *request = CFBridgingRelease(fd->data);
    [request endAccessingResources];
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
};

static const struct fd_ops apkfs_dir_ops = {
    .readdir = apkfs_dir_readdir,
};
static const struct fd_ops apkfs_file_ops = {
    .read = realfs_read,
    .lseek = realfs_lseek,
    .close = realfs_close,
};
