#define CMPEQ(src, dst) \
    dst = dst == src ? (typeof(dst)) -1 : src
#define PCMPEQD(src, dst) \
    xmm_src = get(src); \
    xmm_dst = get(dst); \
    CMPEQ(xmm_src.dw[0], xmm_dst.dw[0]); \
    CMPEQ(xmm_src.dw[1], xmm_dst.dw[1]); \
    CMPEQ(xmm_src.dw[2], xmm_dst.dw[2]); \
    CMPEQ(xmm_src.dw[3], xmm_dst.dw[3]); \
    set(dst, xmm_dst)

// SRL = shift right logical
#define SRL(count, dst) \
    dst >>= count
#define PSRLQ(count, dst) \
    xmm_dst = get(dst); \
    SRL(get(count), xmm_dst.qw[0]); \
    SRL(get(count), xmm_dst.qw[1]); \
    set(dst, xmm_dst)

#define XORP(src, dst) \
    xmm_src = get(src); \
    xmm_dst = get(dst); \
    xmm_dst.qw[0] ^= xmm_src.qw[0]; \
    xmm_dst.qw[1] ^= xmm_src.qw[1]; \
    set(dst, xmm_dst)

#define PADD(src, dst) \
    xmm_src = get(src); \
    xmm_dst = get(dst); \
    xmm_dst.qw[0] += xmm_src.qw[0]; \
    xmm_dst.qw[1] += xmm_src.qw[1]; \
    set(dst, xmm_dst)

#define PSUB(src, dst) \
    xmm_src = get(src); \
    xmm_dst = get(dst); \
    xmm_dst.qw[0] -= xmm_src.qw[0]; \
    xmm_dst.qw[1] -= xmm_src.qw[1]; \
    set(dst, xmm_dst)

#define MOVQ(src, dst) \
    xmm_dst = get(dst); \
    xmm_dst.qw[0] = get(src).qw[0]; \
    if (!is_memory(dst)) \
        xmm_dst.qw[1] = 0; \
    set(dst, xmm_dst)

#define MOVD(src, dst) \
    set(dst, get(src).dw[0])

#include <softfloat.h>
#define CVTTSD2SI(src, dst) \
    set(dst, f64_to_i32(get(src), softfloat_round_minMag, false))
