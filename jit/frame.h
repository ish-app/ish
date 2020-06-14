#ifndef JIT_FRAME_H
#define JIT_FRAME_H

// keep in sync with asm
#define JIT_RETURN_CACHE_SIZE 4096
#define JIT_RETURN_CACHE_HASH(x) ((x) & 0xFFF0) >> 4)

struct jit_frame {
    void *bp;
    addr_t value_addr;
    uint64_t value[2]; // buffer for crosspage crap
    struct jit_block *last_block;
    long ret_cache[JIT_RETURN_CACHE_SIZE]; // a map of ip to pointer-to-call-gadget-arguments
};

#endif
