//
//  FileProviderItem.h
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import <FileProvider/FileProvider.h>
#include "kernel/fs.h"

NS_ASSUME_NONNULL_BEGIN

@interface FileProviderItem : NSObject <NSFileProviderItem>

- (instancetype)initWithIdentifier:(NSFileProviderItemIdentifier)identifier mount:(struct mount *)mount error:(NSError *_Nullable *)err;
- (void)loadToURL:(NSURL *)url;
- (void)saveFromURL:(NSURL *)url;
- (struct fd *)openNewFDWithError:(NSError *_Nullable *)err;

@property (readonly) NSURL *URL;
@property (readonly) NSString *path;
@property (readonly) struct statbuf stat;

@end

NS_ASSUME_NONNULL_END
