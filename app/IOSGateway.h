//
//  IOSGateway.h
//  iSH
//
//  Created by Miguel Vanhove on 15/02/2019.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface IOSGateway : NSObject

+ (IOSGateway *_Nullable ) sharedSession;

- (void)setup;
- (BOOL)handleOpenURL:(NSURL *)url;

@end

extern size_t iac_read(void *buf, size_t bufsize);
extern size_t iac_write(const void *buf, size_t bufsize);

NS_ASSUME_NONNULL_END
