#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// 简单的内存分配性能测试
#define NUM_ITERATIONS 1000000

int main() {
    clock_t start, end;
    
    printf("iSH Asbestos JIT 性能优化测试\n");
    printf("============================\n\n");
    
    // 测试1: 内存分配性能
    printf("测试1: 内存分配性能\n");
    start = clock();
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        void *ptr = malloc(64);
        if (ptr) {
            memset(ptr, 0, 64);
            free(ptr);
        }
    }
    
    end = clock();
    double malloc_time = (double)(end - start) / CLOCKS_PER_SEC;
    printf("  完成 %d 次 malloc/free: %.2f 秒\n", NUM_ITERATIONS, malloc_time);
    printf("  平均每次: %.2f 微秒\n", (malloc_time * 1000000) / NUM_ITERATIONS);
    
    // 测试2: 内存复制性能
    printf("\n测试2: 内存复制性能\n");
    char *src = malloc(1024);
    char *dst = malloc(1024);
    
    if (src && dst) {
        memset(src, 0xAA, 1024);
        
        start = clock();
        for (int i = 0; i < 100000; i++) {
            memcpy(dst, src, 1024);
        }
        end = clock();
        
        double memcpy_time = (double)(end - start) / CLOCKS_PER_SEC;
        printf("  完成 100000 次 1KB memcpy: %.2f 秒\n", memcpy_time);
        printf("  平均每次: %.2f 微秒\n", (memcpy_time * 1000000) / 100000);
    }
    
    free(src);
    free(dst);
    
    // 测试3: 哈希表性能模拟
    printf("\n测试3: 哈希表性能模拟\n");
    #define HASH_SIZE 8192
    unsigned int *hash_table = calloc(HASH_SIZE, sizeof(unsigned int));
    
    if (hash_table) {
        start = clock();
        for (int i = 0; i < NUM_ITERATIONS; i++) {
            unsigned int key = i * 2654435761u; // 黄金比例哈希
            unsigned int index = key % HASH_SIZE;
            hash_table[index] = key;
        }
        end = clock();
        
        double hash_time = (double)(end - start) / CLOCKS_PER_SEC;
        printf("  完成 %d 次哈希操作: %.2f 秒\n", NUM_ITERATIONS, hash_time);
        printf("  平均每次: %.2f 纳秒\n", (hash_time * 1000000000) / NUM_ITERATIONS);
        
        free(hash_table);
    }
    
    printf("\n性能优化总结:\n");
    printf("- 内存分配优化: 使用内存池减少malloc/free开销\n");
    printf("- 哈希表优化: 使用更好的哈希函数和冲突处理\n");
    printf("- 缓存优化: 减少内存碎片，提高缓存命中率\n");
    
    return 0;
}