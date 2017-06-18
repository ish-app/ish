#define CMPEQ(src, dst) \
    dst = dst == src ? (typeof(dst)) -1 : src
#define PCMPEQD(src, dst) \
    CMPEQ(src->dw[0], dst->dw[0]); \
    CMPEQ(src->dw[1], dst->dw[1]); \
    CMPEQ(src->dw[2], dst->dw[2]); \
    CMPEQ(src->dw[3], dst->dw[3])

#define SRL(count, dst) \
    dst >>= count
#define PSRLQ(count, dst) \
    SRL(count, dst->qw[0]); \
    SRL(count, dst->qw[1]); \

#define MOVP(src, dst) \
    dst->qw[0] = src->qw[0]; \
    dst->qw[1] = src->qw[1]

#define XORP(src, dst) \
    dst->qw[0] ^= src->qw[0]; \
    dst->qw[1] ^= src->qw[1]

#define PADD(src, dst) \
    dst->qw[0] += src->qw[0]; \
    dst->qw[1] += src->qw[1]
