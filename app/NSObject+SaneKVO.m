//
//  NSObject+SaneKVO.m
//  iSH
//
//  Created by Theodore Dubois on 11/10/20.
//

#import <objc/runtime.h>
#import "NSObject+SaneKVO.h"

static void *kKVOObservations = &kKVOObservations;

@interface KVOObservation ()
- (instancetype)initWithKeyPath:(NSString *)keyPath object:(id)object block:(void (^)(void))block;
@end

@implementation NSObject (SaneKVO)

- (KVOObservation *)observe:(NSString *)keyPath options:(NSKeyValueObservingOptions)options usingBlock:(void (^)(void))block {
    KVOObservation *observation = [[KVOObservation alloc] initWithKeyPath:keyPath object:self block:block];
    [self addObserver:observation forKeyPath:keyPath options:options context:NULL];
    return observation;
}

- (void)observe:(NSArray<NSString *> *)keyPaths options:(NSKeyValueObservingOptions)options owner:(id)owner usingBlock:(void (^)(id self))block {
    __weak id weakOwner = owner;
    void (^newBlock)(void) = ^{
        id owner = weakOwner;
        NSAssert(owner, @"kvo notification shouldn't come to dead object");
        block(owner);
    };
    @synchronized (owner) {
        for (NSString *keyPath in keyPaths) {
            NSMutableSet *observations = objc_getAssociatedObject(owner, kKVOObservations);
            if (observations == nil) {
                observations = [NSMutableSet new];
                objc_setAssociatedObject(owner, kKVOObservations, observations, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            }
            [observations addObject:[self observe:keyPath options:options usingBlock:newBlock]];
        }
    }
}

@end

@implementation KVOObservation

- (instancetype)initWithKeyPath:(NSString *)keyPath object:(id)object block:(void (^)(void))block {
    if (self = [super init]) {
        _keyPath = keyPath;
        _object = object;
        _block = block;
        _enabled = YES;
    }
    return self;
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    _block();
}

- (void)disable {
    if (_enabled) {
        [_object removeObserver:self forKeyPath:_keyPath context:NULL];
        _enabled = NO;
    }
}
- (void)dealloc {
    [self disable];
}

@end
