//
//  NSObject+SaneKVO.m
//  iSH
//
//  Created by Theodore Dubois on 11/10/20.
//

#import <objc/runtime.h>
#import "NSObject+SaneKVO.h"

static void *kKVOObject = &kKVOObject;

@interface KVOObserver : NSObject {
    @public
    NSString *keyPath;
    void *context;
    __weak id object;
}
@end
@implementation KVOObserver
@end

@interface KVOObject : NSObject

- (instancetype)initWithOwner:(id)owner;
- (BOOL)removeMatchingObserver:(BOOL (^)(KVOObserver *))test;
@property (nonatomic, weak) id owner;
@property NSMutableArray<KVOObserver *> *observers;

@end

@implementation NSObject (SaneKVO)

- (id)sane_createObserver {
    @synchronized (self) {
        KVOObject *observer = objc_getAssociatedObject(self, kKVOObject);
        if (observer == nil) {
            observer = [[KVOObject alloc] initWithOwner:self];
            objc_setAssociatedObject(self, kKVOObject, observer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        return observer;
    }
}
- (id)sane_observer {
    return objc_getAssociatedObject(self, kKVOObject);
}

- (void)sane_addObserver:(NSObject *)observer forKeyPath:(NSString *)keyPath options:(NSKeyValueObservingOptions)options context:(void *)context {
    KVOObject *o = [observer sane_createObserver];
    @synchronized (o) {
        KVOObserver *obs = [KVOObserver new];
        obs->object = self;
        obs->keyPath = keyPath;
        obs->context = context;
        [o.observers addObject:obs];
    }
    [self sane_addObserver:o forKeyPath:keyPath options:options context:context];
}
- (void)sane_removeObserver:(NSObject *)observer forKeyPath:(NSString *)keyPath {
    KVOObject *o = [observer sane_observer];
    if ([o removeMatchingObserver:^BOOL(KVOObserver *obs) {
        return [obs->keyPath isEqualToString:keyPath];
    }]) {
        [self sane_removeObserver:o forKeyPath:keyPath];
    }
}
- (void)sane_removeObserver:(NSObject *)observer forKeyPath:(NSString *)keyPath context:(void *)context {
    KVOObject *o = [observer sane_observer];
    if ([o removeMatchingObserver:^BOOL(KVOObserver *obs) {
        return [obs->keyPath isEqualToString:keyPath] && obs->context == context;
    }]) {
        [self sane_removeObserver:o forKeyPath:keyPath context:context];
    }
}

+ (void)load {
    [self swizzle:@selector(addObserver:forKeyPath:options:context:) with:@selector(sane_addObserver:forKeyPath:options:context:)];
    [self swizzle:@selector(removeObserver:forKeyPath:context:) with:@selector(sane_removeObserver:forKeyPath:context:)];
    [self swizzle:@selector(removeObserver:forKeyPath:) with:@selector(sane_removeObserver:forKeyPath:)];
}
+ (void)swizzle:(SEL)method with:(SEL)replacement {
    method_exchangeImplementations(class_getInstanceMethod(self, method),
                                   class_getInstanceMethod(self, replacement));
}

@end

@implementation KVOObject

- (instancetype)initWithOwner:(id)owner {
    if (self = [super init]) {
        _owner = owner;
        _observers = [NSMutableArray new];
    }
    return self;
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    [_owner observeValueForKeyPath:keyPath ofObject:object change:change context:context];
}

- (BOOL)removeMatchingObserver:(BOOL (^)(KVOObserver *))test {
    @synchronized (self) {
        NSUInteger i = [_observers indexOfObjectPassingTest:^BOOL(KVOObserver * _Nonnull obj, NSUInteger idx, BOOL * _Nonnull stop) {
            return test(obj);
        }];
        if (i == NSNotFound)
            return NO;
        [_observers removeObjectAtIndex:i];
        return YES;
    }
}

- (void)dealloc {
    for (KVOObserver *obs in _observers) {
        [obs->object removeObserver:self forKeyPath:obs->keyPath context:obs->context   ];
    }
}

@end
