#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "float80.h"

// If you don't understand why something is the way it is, change it and run
// the test suite and all will become clear.

// exponent is stored with a constant added to it, because that's apparently
// easier than just saying the exponent is two's complement
#define BIAS80 0x3fff
#define EXP_MAX 0x7ffe
#define EXP_MIN 0x0001
#define EXP_SPECIAL 0x7fff
#define EXP_DENORMAL 0
static unsigned bias(int exp) {
    return exp + BIAS80;
}
static int unbias(unsigned exp) {
    return exp - BIAS80;
}
// returns the correct answer for denormal numbers
static int unbias_denormal(unsigned exp) {
    if (exp == EXP_DENORMAL)
        return unbias(EXP_MIN);
    return unbias(exp);
}

#define CURSED_BIT (1ul << 63)

__thread enum f80_rounding_mode f80_rounding_mode;

// shift a 128 bit integer right but using the floating point rounding mode
// used by f80_shift_right and to round the 128-bit result of multiplying significands
static unsigned __int128 u128_shift_right_round(unsigned __int128 i, int shift) {
    switch (f80_rounding_mode) {
        case round_chop:
            i >>= shift;
            break;

        case round_to_nearest:
            // we're going to be shifting stuff by shift - 1, so stay safe
            if (shift == 0)
                break;

            // grab the last bit shifted out
            int last_bit = (i >> (shift - 1)) & 1;
            i >>= shift;
            // if every bit being shifted out is zero except for the last one, round to even
            if ((i & ~(-1ul << (shift - 1))) == 0) {
                if (i & 1)
                    i++;
            // otherwise if last bit is one, round up
            } else if (last_bit) {
                i++;
            }
            break;
    }
    return i;
}

// may overflow
static float80 f80_shift_left(float80 f, int shift) {
    f.signif <<= shift;
    f.exp -= shift;
    return f;
}

// may lose precision
static float80 f80_shift_right(float80 f, int shift) {
    if (shift > 63)
        // shifts beyond the size of the type are undefined behavior
        f.signif = 0;
    else
        f.signif = u128_shift_right_round(f.signif, shift);
    f.exp += shift;
    return f;
}

// a number is unsupported if the cursed bit (first bit of the significand,
// also known as the integer bit) is incorrect. it must be 0 for denormals and
// 1 for any other type of number.
static bool f80_is_supported(float80 f) {
    if (f.exp == EXP_DENORMAL)
        return f.signif >> 63 == 0;
    return f.signif >> 63 == 1;
}

bool f80_isnan(float80 f) {
    return f.exp == EXP_SPECIAL && (f.signif & (-1ul >> 1)) != 0;
}
bool f80_isinf(float80 f) {
    return f.exp == EXP_SPECIAL && (f.signif & (-1ul >> 1)) == 0;
}
static bool f80_iszero(float80 f) {
    return f.exp == EXP_DENORMAL && f.signif == 0;
}

static float80 f80_normalize(float80 f) {
    // this function probably can't handle unsupported numbers
    // except cursed normals which are just unnormals, and working with them is the point of this function
    if (f.exp == EXP_DENORMAL || f.exp == EXP_SPECIAL)
        assert(f80_is_supported(f));

    // denormals (and zero) are already normalized (unlike the name suggests)
    if (f.exp == EXP_DENORMAL)
        return f;
    // shift left as many times as possible without overflow
    // number of leading zeroes = how many times we can shift out a leading digit before overflow
    int shift = __builtin_clzl(f.signif);
    if (f.signif == 0)
        shift = 64; // __builtin_clzl has undefined result with zero
    if (f.exp - shift < EXP_MIN) {
        // if we shifted this much, exponent would go below its minimum
        // so shift as much as possible and create a denormal
        f = f80_shift_left(f, f.exp - EXP_MIN);
        f.exp = EXP_DENORMAL;
        return f;
    }
    return f80_shift_left(f, shift);
}

static float80 u128_normalize_round(unsigned __int128 signif, int exp) {
    // correctly counting leading zeros on a 128-bit int is interesting
    int shift = __builtin_clzl((uint64_t) (signif >> 64));
    if (signif >> 64 == 0)
        shift = 64 + __builtin_clzl((uint64_t) signif);
    if (signif == 0)
        shift = 128;
    // now shift left
    if (exp - shift < unbias(EXP_MIN)) {
        signif <<= exp - unbias(EXP_MIN);
        exp = unbias(EXP_DENORMAL);
    } else {
        signif <<= shift;
        exp -= shift;
    }
    // and round
    float80 f;
    f.exp = bias(exp);
    f.signif = u128_shift_right_round(signif, 64);
    return f;
}

float80 f80_from_int(int64_t i) {
    // stick i in the significand, give it an exponent of 2^63 to offset the
    // implicit binary point after the first bit, and then normalize
    float80 f = {
        .signif = i,
        .exp = bias(63),
        .sign = 0,
    };
    if (i == 0)
        f.exp = 0;
    if (i < 0) {
        f.sign = 1;
        f.signif = -i;
    }
    return f80_normalize(f);
}

int64_t f80_to_int(float80 f) {
    if (!f80_is_supported(f))
        return INT64_MIN; // indefinite
    // if you need an exponent greater than 2^63 to represent this number, it
    // can't be represented as a 64-bit integer
    if (f.exp > bias(63))
        return !f.sign ? INT64_MAX : INT64_MIN;
    // shift right (reduce precision) until the exponent is 2^63
    f = f80_shift_right(f, bias(63) - f.exp);
    // and the answer should be the significand!
    return !f.sign ? f.signif : -f.signif;
}

struct double_bits {
    unsigned long signif:52;
    unsigned exp:11;
    unsigned sign:1;
};
#define EXP64_MAX 0x7fe
#define EXP64_MIN 0x001
#define EXP64_SPECIAL 0x7ff
#define EXP64_DENORMAL 0x000

// unsupported?
float80 f80_from_double(double d) {
    struct double_bits db;
    memcpy(&db, &d, sizeof(db));
    float80 f;

    if (db.exp == EXP64_SPECIAL)
        f.exp = EXP_SPECIAL;
    else if (db.exp == EXP64_DENORMAL)
        // denormals actually have an exponent of EXP_MIN, the special exponent
        // is needed to indicate the integer bit is 0
        // zeroes have the same exponent as denormals but need to be handled
        // differently
        f.exp = db.signif == 0 ? 0 : bias(1 - 0x3ff);
    else
        f.exp = bias((int) db.exp - 0x3ff);

    f.signif = db.signif << 11;
    if (db.exp != EXP64_DENORMAL)
        f.signif |= CURSED_BIT;
    f.sign = db.sign;
    return f80_normalize(f);
}

double f80_to_double(float80 f) {
    if (!f80_is_supported(f))
        return NAN;
    struct double_bits db;
    db.sign = f.sign;
    int new_exp = unbias(f.exp) + 0x3ff;
    if (f.exp == EXP_SPECIAL)
        new_exp = EXP64_SPECIAL;
    else if (new_exp > EXP64_MAX)
        // out of range
        return !f.sign ? INFINITY : -INFINITY;
    if (new_exp <= 0) {
        // number can only be represented in double precision as a denormal
        // shift it enough to make the exponent into EXP64_MIN
        // does it work on numbers that are not denormal but are too small to represent as double?
        f.signif >>= 1;
        f = f80_shift_right(f, -new_exp);
        new_exp = unbias(f.exp) + 0x3ff;
    }
    db.exp = new_exp;
    db.signif = f.signif >> 11;
    double d;
    memcpy(&d, &db, sizeof(db));
    return d;
}

float80 f80_neg(float80 f) {
    f.sign = ~f.sign;
    return f;
}
float80 f80_abs(float80 f) {
    f.sign = 0;
    return f;
}

float80 f80_add(float80 a, float80 b) {
    if (!f80_is_supported(a) || !f80_is_supported(b))
        return F80_NAN;

    // a has larger exponent, b has smaller exponent
    if (a.exp < b.exp) {
        float80 tmp = a;
        a = b;
        b = tmp;
    }

    if (f80_isnan(a))
        return a;

    // reduce the number of cases to deal with
    bool flipped = false;
    if (a.sign) {
        a.sign = ~a.sign;
        b.sign = ~b.sign;
        flipped = true;
    }
    // now either both are positive (addition) or a is positive and b is
    // negative (subtraction)

    // shift b (smaller exponent) right until the exponents are equal
    float80 orig_b = b;
    b = f80_shift_right(b, a.exp - b.exp);
    assert(a.exp == b.exp);

    float80 f = {.exp = a.exp};
    if (!b.sign) {
        // b is postive, so add
        if (__builtin_uaddl_overflow(a.signif, b.signif, &f.signif)) {
            // overflow, shift right by 1 place and set the cursed bit (which the overflow is into)
            // skip the shift if the exponent is special
            if (f.exp != EXP_SPECIAL)
                f = f80_shift_right(f, 1);
            f.signif |= CURSED_BIT;
        }
    } else {
        // b is negative, so subtract

        // infinity - infinity is indefinite, not zero
        if (f80_isinf(a) && f80_isinf(orig_b))
            return F80_NAN;

        if (a.signif >= b.signif) {
            // we can subtract without underflow
            f.signif = a.signif - b.signif;
        } else {
            // the answer will be negative
            f.signif = b.signif - a.signif;
            f.sign = 1;
        }

        if (f.signif == 0) {
            f.exp = 0;
            // only way to get negative zero would be from -0 + -0, which is
            // handled by the other case
            // so skip the flip
            return f;
        }
        f = f80_normalize(f);
    }

    if (flipped)
        f.sign = ~f.sign;
    assert(f80_is_supported(f));
    return f;
}
float80 f80_sub(float80 a, float80 b) {
    return f80_add(a, f80_neg(b));
}

float80 f80_mul(float80 a, float80 b) {
    if (!f80_is_supported(a) || !f80_is_supported(b))
        return F80_NAN;
    if (f80_isnan(a))
        return F80_NAN;
    if (f80_isnan(b))
        return F80_NAN;

    if (f80_isinf(a) || f80_isinf(b)) {
        // infinity times zero is undefined
        if (f80_iszero(a) || f80_iszero(b))
            return F80_NAN;
        // infinity times anything else is infinity
        float80 f = F80_INF;
        f.sign = a.sign ^ b.sign;
        return f;
    }

    // add exponents (the +1 is necessary to be correct in 128-bit precision)
    int f_exp = unbias_denormal(a.exp) + unbias_denormal(b.exp) + 1;
    // multiply significands
    unsigned __int128 f_signif = (unsigned __int128) a.signif * b.signif;
    // normalize and round the 128-bit result
    float80 f = u128_normalize_round(f_signif, f_exp);
    // xor signs
    f.sign = a.sign ^ b.sign;
    return f;
}

// FIXME this is sort of broken for dividing by very small numbers (good enough for now though)
float80 f80_div(float80 a, float80 b) {
    if (!f80_is_supported(a) || !f80_is_supported(b))
        return F80_NAN;
    if (f80_isnan(a))
        return F80_NAN;
    if (f80_isnan(b))
        return F80_NAN;

    float80 f;
    if (f80_isinf(a)) {
        // dividing into infinity gives infinity
        f = F80_INF;
        // except infinity / infinity is nan
        if (f80_isinf(b))
            return F80_NAN;
    } else if (f80_isinf(b)) {
        // dividing by infinity gives zero
        f = (float80) {0};
    } else if (f80_iszero(b)) {
        // division by zero gives infinity
        f = F80_INF;
        // except 0 / 0 is nan
        if (f80_iszero(a))
            f = F80_NAN;
    } else {
        unsigned __int128 signif = ((unsigned __int128) a.signif << 64) / b.signif;
        int exp = unbias_denormal(a.exp) - unbias_denormal(b.exp) + 63;
        f = u128_normalize_round(signif, exp);
    }

    f.sign = a.sign ^ b.sign;
    return f;
}

float80 f80_mod(float80 x, float80 y) {
    float80 quotient = f80_div(x, y);
    enum f80_rounding_mode old_mode = f80_rounding_mode;
    f80_rounding_mode = round_chop;
    quotient = f80_from_int(f80_to_int(quotient)); // TODO just make a f80_round_to_int function
    f80_rounding_mode = old_mode;
    return f80_sub(x, f80_mul(quotient, y));
}

bool f80_lt(float80 a, float80 b) {return false;}
bool f80_gt(float80 a, float80 b) {return false;}
bool f80_eq(float80 a, float80 b) {return false;}
