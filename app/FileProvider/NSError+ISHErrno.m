//
//  NSError+ISHErrno.m
//  iSH
//
//  Created by Theodore Dubois on 12/15/18.
//

#import <FileProvider/FileProvider.h>
#import "NSError+ISHErrno.h"
#include "kernel/errno.h"

@implementation NSError (ISHErrno)

+ (NSError *)errorWithISHErrno:(long)err itemIdentifier:(nonnull NSFileProviderItemIdentifier)identifier {
    switch (err) {
        case _ENOENT:
            return [NSError fileProviderErrorForNonExistentItemWithIdentifier:identifier];
    }
    return [NSError errorWithDomain:ISHErrnoDomain
                               code:err
                           userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:@"error code %ld", err]}];
}

@end

NSString *const ISHErrnoDomain = @"ISHErrnoDomain";
