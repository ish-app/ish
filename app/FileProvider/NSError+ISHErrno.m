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

+ (NSError *)errorWithISHErrno:(long)err {
    switch (err) {
        case _ENOENT:
            return [NSError errorWithDomain:NSFileProviderErrorDomain
                                       code:NSFileProviderErrorNoSuchItem
                                   userInfo:nil];
    }
    return [NSError errorWithDomain:ISHErrnoDomain
                               code:err
                           userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:@"error code %d", err]}];
}

@end

NSString *const ISHErrnoDomain = @"ISHErrnoDomain";
