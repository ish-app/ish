//
//  FileProviderEnumerator.h
//  iSHFiles
//
//  Created by Theodore Dubois on 9/20/18.
//

#import <FileProvider/FileProvider.h>

@interface FileProviderEnumerator : NSObject <NSFileProviderEnumerator>

- (instancetype)init NS_UNAVAILABLE;
- (instancetype)initWithEnumeratedItemIdentifier:(NSFileProviderItemIdentifier)enumeratedItemIdentifier;

@property (nonatomic, readonly, strong) NSFileProviderItemIdentifier enumeratedItemIdentifier;

@end
