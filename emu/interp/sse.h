#define CMPEQ(src, dst) \
    dst = dst == src ? (typeof(dst)) -1 : src
#define PCMPEQD(src, dst) \
    xmm_src = get(src,128); \
    xmm_dst = get(dst,128); \
    CMPEQ(xmm_src.dw[0], xmm_dst.dw[0]); \
    CMPEQ(xmm_src.dw[1], xmm_dst.dw[1]); \
    CMPEQ(xmm_src.dw[2], xmm_dst.dw[2]); \
    CMPEQ(xmm_src.dw[3], xmm_dst.dw[3]); \
    set(dst, xmm_dst,128)

// SRL = shift right logical
#define SRL(count, dst) \
    dst >>= count
#define PSRLQ(count, dst) \
    xmm_dst = get(dst,128); \
    SRL(get(count,), xmm_dst.qw[0]); \
    SRL(get(count,), xmm_dst.qw[1]); \
    set(dst, xmm_dst,128)

#define XORP(src, dst) \
    xmm_src = get(src,128); \
    xmm_dst = get(dst,128); \
    xmm_dst.qw[0] ^= xmm_src.qw[0]; \
    xmm_dst.qw[1] ^= xmm_src.qw[1]; \
    set(dst, xmm_dst,128)

#define PADD(src, dst) \
    xmm_src = get(src,128); \
    xmm_dst = get(dst,128); \
    xmm_dst.qw[0] += xmm_src.qw[0]; \
    xmm_dst.qw[1] += xmm_src.qw[1]; \
    set(dst, xmm_dst,128)

#define PSUB(src, dst) \
    xmm_src = get(src,128); \
    xmm_dst = get(dst,128); \
    xmm_dst.qw[0] -= xmm_src.qw[0]; \
    xmm_dst.qw[1] -= xmm_src.qw[1]; \
    set(dst, xmm_dst,128)

#define MOVQ(src, dst) \
    xmm_dst = get(dst,128); \
    xmm_dst.qw[0] = get(src,128).qw[0]; \
    if (!is_memory(dst)) \
        xmm_dst.qw[1] = 0; \
    set(dst, xmm_dst,128)

#define MOVD(src, dst) \
    set(dst, get(src,128).dw[0],32)

#include "emu/float80.h"
#define CVTTSD2SI(src, dst) \
    set(dst, (int32_t) get(src,64),32)
