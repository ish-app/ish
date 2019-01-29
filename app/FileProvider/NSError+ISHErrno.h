//
//  NSError+ISHErrno.h
//  iSH
//
//  Created by Theodore Dubois on 12/15/18.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSError (ISHErrno)

+ (NSError *)errorWithISHErrno:(long)err;

@end

extern NSString *const ISHErrnoDomain;

NS_ASSUME_NONNULL_END
