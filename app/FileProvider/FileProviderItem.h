//
//  FileProviderItem.h
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import <FileProvider/FileProvider.h>

@interface FileProviderItem : NSObject <NSFileProviderItem>

- (instancetype)initWithIdentifier:(NSFileProviderItemIdentifier)identifier;

@end
