#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "float80.h"
#include "misc.h"

typedef unsigned __int128 uint128_t;

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

static bool round_away_from_zero(int sign) {
    return (f80_rounding_mode == round_up && !sign) ||
        (f80_rounding_mode == round_down && sign);
}

// shift a 128 bit integer right but using the floating point rounding mode
// used by f80_shift_right and to round the 128-bit result of multiplying significands
// sign is necessary to decide which way to round when mode is round_up or round_down
static uint128_t u128_shift_right_round(uint128_t i, int shift, int sign) {
    // we're going to be shifting stuff by shift - 1, so stay safe
    if (shift == 0)
        return i;
    if (shift > 127) {
        // If we should be rounding away from zero, and there are any nonzero
        // bits involved, an infinite amount of right shift should give 1
        if (round_away_from_zero(sign) && i != 0)
            return 1;
        return 0;
    }

    // stuff necessary for rounding to nearest or even. reference: https://stackoverflow.com/a/8984135
    // grab the guard bit, the last bit shifted out
    int guard = (i >> (shift - 1)) & 1;
    // now grab the rest of the bits being shifted out
    uint64_t rest = i & ~((uint128_t) -1 << (shift - 1));

    i >>= shift;
    // if all the bits shifted out were zeroes, we're done
    if (guard == 0 && rest == 0)
        return i;

    if (round_away_from_zero(sign)) {
        i++;
    } else if (f80_rounding_mode == round_to_nearest && guard) {
        if (rest != 0)
            i++; // round up
        else if (i & 1)
            i++; // round to nearest even
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
    f.signif = u128_shift_right_round(f.signif, shift, f.sign);
    f.exp += shift;
    return f;
}

// a number is unsupported if the cursed bit (first bit of the significand,
// also known as the integer bit) is incorrect. it must be 0 for denormals and
// 1 for any other type of number.
bool f80_is_supported(float80 f) {
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
bool f80_iszero(float80 f) {
    return f.exp == EXP_DENORMAL && f.signif == 0;
}
bool f80_isdenormal(float80 f) {
    return f.exp == EXP_DENORMAL && f.signif != 0;
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
    int shift;
    if (f.signif != 0)
        shift = __builtin_clzl(f.signif);
    else
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

static int u128_clz(uint128_t x) {
    // correctly counting leading zeros on a 128-bit int is interesting
    int zeros;
    if (x >> 64 != 0)
        zeros = __builtin_clzl((uint64_t) (x >> 64));
    else if (x != 0)
        zeros = 64 + __builtin_clzl((uint64_t) x);
    else
        zeros = 128;
    return zeros;
}

static float80 u128_normalize_round(uint128_t signif, int exp, int sign) {
    int shift = u128_clz(signif);
    // now shift left
    if (exp - shift < unbias(EXP_MIN)) {
        if (exp > unbias(EXP_MIN))
            signif <<= exp - unbias(EXP_MIN);
        else
            signif = u128_shift_right_round(signif, unbias(EXP_MIN) - exp, sign);
        exp = unbias(EXP_DENORMAL);
    } else if (exp - shift > unbias(EXP_MAX)) {
        //printf("0x%.16llx%.16llx ", (unsigned long long) (signif >> 64), (unsigned long long) signif);
        // too big to represent, so either construct infinity, or round it to the largest representable number
        float80 f;
        if (signif == ((uint128_t) 1 << 127))
            f = F80_INF;
        else if ((f80_rounding_mode == round_up && sign) || (f80_rounding_mode == round_down && !sign) || f80_rounding_mode == round_chop)
            f = (float80) {.exp = EXP_MAX, .signif = -1};
        else
            f = F80_INF;
        f.sign = sign;
        return f;
    } else {
        signif <<= shift;
        exp -= shift;
    }
    // and round
    float80 f;
    f.exp = bias(exp);
    // hack around cases where u128_shift_right_round returns 0x10000000000000000
    // such as signif = 0xffffffffffffffff0000000000000000
    signif = u128_shift_right_round(signif, 64, sign);
    if (signif >> 64 != 0) {
        signif >>= 1;
        f.exp++;
    }
    f.signif = signif;
    f.sign = sign;
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
        f.signif = -(uint64_t) i;
    }
    return f80_normalize(f);
}

int64_t f80_to_int(float80 f) {
    if (!f80_is_supported(f))
        return INT64_MIN; // indefinite
    // if you need an exponent greater than 2^63 to represent this number, it
    // can't be represented as a 64-bit integer
    if (f.exp > bias(63))
        return INT64_MIN; // also indefinite
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

    f.signif = (uint64_t) db.signif << 11;
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
    uint64_t db_signif = u128_shift_right_round(f.signif, 11, f.sign);
    // handle the case when f.signif becomes 0x1fffffffffffff after shifting
    // and then is rounded up
    if (db_signif & (1ul << 53)) {
        db_signif >>= 1;
        db.exp++;
    }
    db.signif = db_signif;
    double d;
    memcpy(&d, &db, sizeof(db));
    return d;
}

float80 f80_round(float80 f) {
    if (!f80_is_supported(f))
        return F80_NAN;
    // Shift out all the bits to the right of the point (early exit if there are none)
    int bits_to_clear = 63 - unbias(f.exp);
    if (bits_to_clear <= 0)
        return f;

    f = f80_shift_right(f, bits_to_clear);
    if (f.signif == 0) {
        // If that just totally eradicated the significand, guess the answer is 0
        f.exp = EXP_DENORMAL;
    } else {
        f = f80_normalize(f);
    }
    return f;
}

float80 f80_neg(float80 f) {
    f.sign = ~f.sign;
    return f;
}
float80 f80_abs(float80 f) {
    f.sign = 0;
    return f;
}

#define handle_nans(a, b) do { \
    if (!f80_is_supported(a) || !f80_is_supported(b)) \
        return F80_NAN; \
    /* this case is bizarre but hey I don't make the chips. though the amd spec
     * says it's undefined which nan is returned if both have the same
     * significant and different sign, so why am I doing this */\
    if (f80_isnan(a) && f80_isnan(b) && a.sign && !b.sign) \
        return b; \
    if (f80_isnan(a)) \
        return a; \
    if (f80_isnan(b)) \
        return b; \
} while(0)

float80 f80_add(float80 a, float80 b) {
    handle_nans(a, b);

    // a has larger exponent, b has smaller exponent
    if (a.exp < b.exp) {
        float80 tmp = a;
        a = b;
        b = tmp;
    }

    // reduce the number of cases to deal with
    bool flipped = false;
    if (a.sign) {
        a.sign = ~a.sign;
        b.sign = ~b.sign;
        flipped = true;
    }
    // now either both are positive (addition) or a is positive and b is
    // negative (subtraction)

    // do the addition in insane precision to fix that bug with adding 2^64 and 1.5
    uint128_t a_signif = (uint128_t) a.signif << 64;
    uint128_t b_signif = (uint128_t) b.signif << 64;
    // shift b (smaller exponent) right until the exponents are equal
    b_signif = u128_shift_right_round(b_signif, a.exp - b.exp, b.sign ^ flipped);

    int sign = a.sign;
    int exp = unbias_denormal(a.exp);
    uint128_t signif = a_signif;
    if (!b.sign) {
        // b is positive, so add
        if (!f80_isinf(a)) {
            if (__builtin_add_overflow(a_signif, b_signif, &signif)) {
                // in case of overflow, lose 1 bit of precision
                signif = u128_shift_right_round(signif, 1, sign);
                signif |= (uint128_t) 1 << 127; // recover the bit lost by the overflow
                exp++;
            }
        }
    } else {
        // b is negative, so subtract
        // but first, special case time!

        // infinity - infinity is indefinite, not zero
        if (f80_isinf(a) && f80_isinf(b))
            return F80_NAN;

        // When subtracting a (relatively) very small number in chop mode, all
        // the bits will get shifted out and nothing will happen, but this
        // should give a smaller result.
        if (f80_rounding_mode == round_chop && b_signif == 0 && b.signif != 0)
            b_signif = 1;

        // Depending on the rounding mode, it's possible that shifting out all
        // the bits produced 1 instead of 0. Subtracting 1 from infinity would
        // give 2^16384-1 which is wrong.
        if (f80_isinf(a))
            b_signif = 0;

        if (a_signif >= b_signif) {
            // we can subtract without underflow
            signif = a_signif - b_signif;
        } else {
            // the answer will be negative
            signif = b_signif - a_signif;
            sign = 1;
        }

        // a bizarre special case https://twitter.com/tblodt/status/1262145524620234752
        if (signif == 0 && a_signif != 0 && f80_rounding_mode == round_down)
            return (float80) {.sign = 1};

        // a - a = 0
        if (signif == 0)
            return (float80) {0};
    }

    if (flipped)
        sign = !sign;
    float80 f = u128_normalize_round(signif, exp, sign);
    assert(f80_is_supported(f));
    return f;
}
float80 f80_sub(float80 a, float80 b) {
    return f80_add(a, f80_neg(b));
}

float80 f80_mul(float80 a, float80 b) {
    handle_nans(a, b);

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
    uint128_t f_signif = (uint128_t) a.signif * b.signif;
    // normalize and round the 128-bit result
    float80 f = u128_normalize_round(f_signif, f_exp, a.sign ^ b.sign);
    // xor signs
    f.sign = a.sign ^ b.sign;
    return f;
}

float80 f80_div(float80 a, float80 b) {
    handle_nans(a, b);

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
        int b_trailing = __builtin_ctzl(b.signif);
        b.signif >>= b_trailing;
        uint128_t signif = ((uint128_t) a.signif << 64) / b.signif;
        uint128_t remainder = ((uint128_t) a.signif << 64) % b.signif;
        // extend this to 128 bit precision because hell yeah
        int extra_bits = 0;
        if (signif != 0) {
            extra_bits = u128_clz(signif);
            signif <<= extra_bits;
            signif |= (remainder << extra_bits) / b.signif;
        }
        int exp = unbias_denormal(a.exp) - unbias_denormal(b.exp) + 63 - b_trailing - extra_bits;
        f = u128_normalize_round(signif, exp, a.sign ^ b.sign);
    }

    f.sign = a.sign ^ b.sign;
    return f;
}

float80 f80_mod(float80 x, float80 y) {
    float80 quotient = f80_div(x, y);
    enum f80_rounding_mode old_mode = f80_rounding_mode;
    f80_rounding_mode = round_chop;
    quotient = f80_round(quotient);
    f80_rounding_mode = old_mode;
    return f80_sub(x, f80_mul(quotient, y));
}

bool f80_uncomparable(float80 a, float80 b) {
    if (!f80_is_supported(a) || !f80_is_supported(b))
        return true;
    if (f80_isnan(a) || f80_isnan(b))
        return true;
    return false;
}

bool f80_lt(float80 a, float80 b) {
    if (f80_uncomparable(a, b))
        return false;
    // same signed infinities are equal, not less (though subtraction would produce nan)
    if (f80_isinf(a) && f80_isinf(b) && a.sign == b.sign)
        return false;
    // zeroes are always equal
    if (f80_iszero(a) && f80_iszero(b))
        return false;
    // if a < b then a - b < 0
    float80 diff = f80_sub(a, b);
    return diff.sign == 1 && !f80_iszero(diff);
}
bool f80_eq(float80 a, float80 b) {
    if (f80_uncomparable(a, b))
        return false;
    if (f80_iszero(a)) a.sign = 0;
    if (f80_iszero(a)) b.sign = 0;
    return a.sign == b.sign && a.exp == b.exp && a.signif == b.signif;
}

bool f80_lte(float80 a, float80 b) {
    return f80_lt(a, b) || f80_eq(a, b);
}
bool f80_gt(float80 a, float80 b) {
    return !f80_lte(a, b);
}

float80 f80_log2(float80 x) {
    float80 zero = f80_from_int(0);
    float80 one = f80_from_int(1);
    float80 two = f80_from_int(2);
    if (f80_isnan(x) || f80_lte(x, zero))
        return F80_NAN;

    int ipart = 0;
    while (f80_lt(x, one)) {
        ipart--;
        x = f80_mul(x, two);
    }
    while (f80_gt(x, two)) {
        ipart++;
        x = f80_div(x, two);
    }
    float80 res = f80_from_int(ipart);

    float80 bit = one;
    while (f80_gt(bit, zero)) {
        while (f80_lte(x, two) && f80_gt(bit, zero)) {
            x = f80_mul(x, x);
            bit = f80_div(bit, two);
        }
        float80 oldres = res;
        res = f80_add(res, bit);
        if (oldres.signif == res.signif && oldres.exp == res.exp && oldres.sign == res.sign)
            break;
        x = f80_div(x, two);
    }
    return res;
}

float80 f80_sqrt(float80 x) {
    if (f80_iszero(x))
        return x;
    if (f80_isnan(x) || x.sign)
        return F80_NAN;
    // for a rough guess, just cut the exponent by 2
    float80 guess = x;
    guess.exp = bias(unbias(guess.exp) / 2);
    // now converge on the answer, using newton's method
    float80 old_guess;
    float80 two = f80_from_int(2);
    int i = 0;
    do {
        old_guess = guess;
        guess = f80_div(f80_add(guess, f80_div(x, guess)), two);
    } while (!f80_eq(guess, old_guess) && i++ < 100);
    return guess;
}

float80 f80_scale(float80 x, int scale) {
    if (!f80_is_supported(x) || f80_isnan(x))
        return F80_NAN;
    return u128_normalize_round((uint128_t) x.signif << 64, unbias(x.exp) + scale, x.sign);
}

void f80_xtract(float80 f, int *exp, float80 *signif) {
    *exp = unbias(f.exp);
    *signif = f;
    signif->exp = bias(0);
}
