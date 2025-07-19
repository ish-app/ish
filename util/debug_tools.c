#include "ish_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

// 调试和性能分析工具
static volatile int debug_enabled = 0;

void ish_debug_enable(void) {
    debug_enabled = 1;
}

void ish_debug_disable(void) {
    debug_enabled = 0;
}

// 内存泄漏检测
#ifdef ISH_ENABLE_MEMORY_DEBUG
#include <execinfo.h>

#define MAX_BACKTRACE 10

typedef struct memory_allocation {
    void *ptr;
    size_t size;
    void *backtrace[MAX_BACKTRACE];
    int backtrace_size;
    struct memory_allocation *next;
} memory_allocation_t;

static memory_allocation_t *allocations = NULL;
static size_t total_allocated = 0;

void *ish_debug_malloc(size_t size, const char *file, int line) {
    void *ptr = malloc(size);
    if (ptr && debug_enabled) {
        memory_allocation_t *alloc = malloc(sizeof(memory_allocation_t));
        alloc->ptr = ptr;
        alloc->size = size;
        alloc->backtrace_size = backtrace(alloc->backtrace, MAX_BACKTRACE);
        alloc->next = allocations;
        allocations = alloc;
        total_allocated += size;
        
        fprintf(stderr, "[MEM] Alloc %p (%zu bytes) at %s:%d\n", ptr, size, file, line);
    }
    return ptr;
}

void ish_debug_free(void *ptr, const char *file, int line) {
    if (debug_enabled) {
        memory_allocation_t **current = &allocations;
        while (*current) {
            if ((*current)->ptr == ptr) {
                memory_allocation_t *to_free = *current;
                *current = (*current)->next;
                total_allocated -= to_free->size;
                fprintf(stderr, "[MEM] Free %p (%zu bytes) at %s:%d\n", ptr, to_free->size, file, line);
                free(to_free);
                break;
            }
            current = &(*current)->next;
        }
    }
    free(ptr);
}

void ish_debug_print_leaks(void) {
    if (allocations) {
        fprintf(stderr, "\n=== MEMORY LEAKS DETECTED ===\n");
        fprintf(stderr, "Total leaked: %zu bytes\n", total_allocated);
        
        memory_allocation_t *current = allocations;
        while (current) {
            fprintf(stderr, "Leak: %p (%zu bytes)\n", current->ptr, current->size);
            char **symbols = backtrace_symbols(current->backtrace, current->backtrace_size);
            for (int i = 0; i < current->backtrace_size; i++) {
                fprintf(stderr, "  %s\n", symbols[i]);
            }
            free(symbols);
            current = current->next;
        }
    }
}
#endif

// 信号处理
static void ish_signal_handler(int sig) {
    fprintf(stderr, "\n=== iSH Debug Signal Handler ===\n");
    fprintf(stderr, "Received signal %d\n", sig);
    
#ifdef ISH_ENABLE_MEMORY_DEBUG
    ish_debug_print_leaks();
#endif
    
    exit(1);
}

void ish_debug_init(void) {
    signal(SIGSEGV, ish_signal_handler);
    signal(SIGABRT, ish_signal_handler);
    signal(SIGINT, ish_signal_handler);
}

// 性能计时器
#ifdef ISH_ENABLE_TIMING
#include <time.h>

typedef struct timing_point {
    const char *name;
    struct timespec start;
    struct timespec end;
    struct timing_point *next;
} timing_point_t;

static timing_point_t *timing_points = NULL;

void ish_timing_start(const char *name) {
    if (!debug_enabled) return;
    
    timing_point_t *point = malloc(sizeof(timing_point_t));
    point->name = name;
    clock_gettime(CLOCK_MONOTONIC, &point->start);
    point->next = timing_points;
    timing_points = point;
}

void ish_timing_end(const char *name) {
    if (!debug_enabled) return;
    
    timing_point_t *current = timing_points;
    while (current) {
        if (current->name == name) {
            clock_gettime(CLOCK_MONOTONIC, &current->end);
            double elapsed = (current->end.tv_sec - current->start.tv_sec) +
                           (current->end.tv_nsec - current->start.tv_nsec) / 1e9;
            fprintf(stderr, "[TIME] %s: %.6f seconds\n", name, elapsed);
            break;
        }
        current = current->next;
    }
}
#endif