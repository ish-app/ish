#ifndef ISH_COMMON_H
#define ISH_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// 通用宏定义
#define likely(x)       __builtin_expect(!!(x), 1)
#ifndef unlikely
#define unlikely(x)     __builtin_expect(!!(x), 0)
#endif
#ifndef UNUSED
#define UNUSED(x)       ((void)(x))
#endif
#define ARRAY_SIZE(x)   (sizeof(x) / sizeof((x)[0]))

// 内存分配优化
static inline void *ish_malloc(size_t size) {
    return malloc(size);
}

static inline void *ish_calloc(size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}

static inline void *ish_realloc(void *ptr, size_t size) {
    return realloc(ptr, size);
}

static inline void ish_free(void *ptr) {
    free(ptr);
}

// 性能计数器
#ifdef ISH_ENABLE_PERF_COUNTERS
extern uint64_t ish_perf_counters[];
enum {
    PERF_COUNTER_TLB_HITS,
    PERF_COUNTER_TLB_MISSES,
    PERF_COUNTER_MEMORY_ALLOC,
    PERF_COUNTER_MEMORY_FREE,
    PERF_COUNTER_LAST
};

#define PERF_INC(counter) do { \
    if (ish_perf_counters) ish_perf_counters[counter]++; \
} while (0)

#else
#define PERF_INC(counter) do {} while (0)
#endif

// 缓存行对齐
#define CACHE_LINE_SIZE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

// 分支预测优化
#define ISH_LIKELY(x)   __builtin_expect(!!(x), 1)
#define ISH_UNLIKELY(x) __builtin_expect(!!(x), 0)

// 内联优化
#ifdef __GNUC__
#define ISH_INLINE static inline __attribute__((always_inline))
#else
#define ISH_INLINE static inline
#endif

// 内存屏障
#define ISH_MEMORY_BARRIER() __asm__ volatile("" ::: "memory")

#endif // ISH_COMMON_H