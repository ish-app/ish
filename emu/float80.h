#ifndef FLOAT80_H
#define FLOAT80_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t signif;
    union {
        uint16_t signExp;
        struct {
            unsigned exp:15;
            unsigned sign:1;
        };
    };
} float80;

float80 f80_from_int(int64_t i);
int64_t f80_to_int(float80 f);
float80 f80_from_double(double d);
double f80_to_double(float80 f);

bool f80_isnan(float80 f);
bool f80_isinf(float80 f);

float80 f80_neg(float80 f);
float80 f80_abs(float80 f);

float80 f80_add(float80 a, float80 b);
float80 f80_sub(float80 a, float80 b);
float80 f80_mul(float80 a, float80 b);
float80 f80_div(float80 a, float80 b);
float80 f80_mod(float80 a, float80 b);
float80 f80_rem(float80 a, float80 b);

bool f80_lt(float80 a, float80 b);
bool f80_gt(float80 a, float80 b);
bool f80_eq(float80 a, float80 b);

enum f80_rounding_mode {
    round_to_nearest = 0,
    round_chop = 3,
};
extern __thread enum f80_rounding_mode f80_rounding_mode;

#define F80_NAN ((float80) {.signif = 0xc000000000000000, .exp = 0x7fff, .sign = 0})
#define F80_INF ((float80) {.signif = 0x8000000000000000, .exp = 0x7fff, .sign = 0})

#endif
