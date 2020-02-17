#ifndef UTIL_REFCOUNT_H
#define UTIL_REFCOUNT_H
#include <stdatomic.h>

// An industrial-strength refcounting implementation.
// Safety first! Make sure to:
// - Write a release for every retain
// - Get exclusive access to the reference before retaining or releasing it

struct refcount {
    atomic_uint rc;
};

static inline void __refcount_init(struct refcount *refcount) {
    refcount->rc = 1;
}
#define refcount_init(obj) __refcount_init(&(obj)->refcount)

static inline int __refcount_get(struct refcount *refcount) {
    return refcount->rc;
}
#define refcount_get(obj) __refcount_get(&(obj)->refcount)

#define DECLARE_REFCOUNT(type) \
    void type##_retain(struct type *obj); \
    void type##_release(struct type *obj)

#define __DEFINE_REFCOUNT(type, qualifiers) \
    qualifiers struct type *type##_retain(struct type *obj) { \
        obj->refcount.rc++; \
        return obj; \
    } \
    static void type##_cleanup(struct type *obj); \
    qualifiers void type##_release(struct type *obj) { \
        if (--obj->refcount.rc == 0) \
            type##_cleanup(obj); \
    }

#define DEFINE_REFCOUNT(type) __DEFINE_REFCOUNT(type, )
#define DEFINE_REFCOUNT_STATIC(type) __DEFINE_REFCOUNT(type, static)

#endif
