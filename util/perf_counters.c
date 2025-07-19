#include "ish_common.h"
#include <stdio.h>
#include <time.h>

#ifdef ISH_ENABLE_PERF_COUNTERS
uint64_t ish_perf_counters[PERF_COUNTER_LAST] = {0};

void ish_perf_print_stats(void) {
    static time_t last_print = 0;
    time_t now = time(NULL);
    
    if (now - last_print >= 1) { // 每秒打印一次
        fprintf(stderr, "iSH Performance Stats:\n");
        fprintf(stderr, "  TLB Hits: %lu\n", ish_perf_counters[PERF_COUNTER_TLB_HITS]);
        fprintf(stderr, "  TLB Misses: %lu\n", ish_perf_counters[PERF_COUNTER_TLB_MISSES]);
        fprintf(stderr, "  Memory Allocs: %lu\n", ish_perf_counters[PERF_COUNTER_MEMORY_ALLOC]);
        fprintf(stderr, "  Memory Frees: %lu\n", ish_perf_counters[PERF_COUNTER_MEMORY_FREE]);
        
        // 重置计数器
        memset(ish_perf_counters, 0, sizeof(ish_perf_counters));
        last_print = now;
    }
}

void ish_perf_reset(void) {
    memset(ish_perf_counters, 0, sizeof(ish_perf_counters));
}
#endif