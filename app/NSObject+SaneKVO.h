//
//  NSObject+SaneKVO.h
//  iSH
//
//  Created by Theodore Dubois on 11/10/20.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface KVOObservation : NSObject {
    BOOL _enabled;
    __weak id _object;
    NSString *_keyPath;
    void (^_block)(void);
}
- (void)disable;
@end

@interface NSObject (SaneKVO)

- (KVOObservation *)observe:(NSString *)keyPath
                    options:(NSKeyValueObservingOptions)options
                 usingBlock:(void (^)(void))block;
- (void)observe:(NSArray<NSString *> *)keyPaths
        options:(NSKeyValueObservingOptions)options
          owner:(id)owner
     usingBlock:(void (^)(id))block;

@end

NS_ASSUME_NONNULL_END
