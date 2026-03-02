//
//  FileProviderExtension.h
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import <FileProvider/FileProvider.h>
#include "fs/fake-db.h"

struct fakefs_mount {
    struct fakefs_db db;
    int root_fd;
    const char *source;
};

@interface FileProviderExtension : NSFileProviderExtension

- (struct fakefs_mount *)mount;

@end
