//
//  FileProviderExtension.h
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import <FileProvider/FileProvider.h>
#include "kernel/task.h"

@interface FileProviderExtension : NSFileProviderExtension

@end

extern struct task *fake_task;
