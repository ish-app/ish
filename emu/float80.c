#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "float80.h"
#include "misc.h"

#define F80_QUIET_BIT 0x4000000000000000
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
    if (signif == 0)
        // an exact zero (0 * x, 0 / x): there is nothing to normalize
        return (float80) {.signif = 0, .exp = 0, .sign = (unsigned) sign};
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
    if (db.exp == EXP64_SPECIAL && db.signif != 0) {
        // loading a NaN makes it quiet and keeps its payload
        f.signif |= F80_QUIET_BIT;
        return f;
    }
    return f80_normalize(f);
}

// Convert to an IEEE format with the given number of fraction bits (52 for
// double, 23 for float), rounding once, in the current rounding mode.
static uint64_t f80_to_ieee(float80 f, int frac_bits, int exp_bits) {
    const unsigned exp_special = (1u << exp_bits) - 1;
    const int exp_bias = (int) (exp_special >> 1);
    uint64_t sign = (uint64_t) f.sign << (frac_bits + exp_bits);
    if (!f80_is_supported(f) || f80_isnan(f)) {
        // quiet, with the top of the payload
        uint64_t payload = f80_is_supported(f) ? (f.signif << 1) >> (64 - frac_bits) : 0;
        return sign | ((uint64_t) exp_special << frac_bits) | ((uint64_t) 1 << (frac_bits - 1)) | payload;
    }
    if (f80_isinf(f))
        return sign | ((uint64_t) exp_special << frac_bits);
    if (f.signif == 0)
        return sign;
    // normalize: value = sig * 2^(e - 63) with the top bit of sig set
    uint64_t sig = f.signif;
    int e = unbias_denormal(f.exp);
    int lz = __builtin_clzl(sig);
    sig <<= lz;
    e -= lz;
    int target_exp = e + exp_bias; // biased exponent in the target format
    // the result keeps frac_bits + 1 bits of sig; a denormal keeps fewer
    int shift = 63 - frac_bits;
    if (target_exp <= 0)
        shift += 1 - target_exp;
    uint64_t mant, rest_guard, rest_sticky;
    if (shift >= 65) {
        mant = 0; rest_guard = 0; rest_sticky = 1;
    } else if (shift == 64) {
        mant = 0; rest_guard = sig >> 63; rest_sticky = (sig << 1) != 0;
    } else {
        mant = sig >> shift;
        rest_guard = (sig >> (shift - 1)) & 1;
        rest_sticky = (sig & (((uint64_t) 1 << (shift - 1)) - 1)) != 0;
    }
    bool inexact = rest_guard || rest_sticky;
    bool up = false;
    switch (f80_rounding_mode) {
        case round_to_nearest: up = rest_guard && (rest_sticky || (mant & 1)); break;
        case round_down: up = f.sign && inexact; break;
        case round_up: up = !f.sign && inexact; break;
        case round_chop: break;
    }
    if (up)
        mant++;
    // mant includes the integer bit for normal numbers; adding it to the
    // exponent field makes a carry out of the fraction bump the exponent
    uint64_t bits;
    if (target_exp <= 0)
        bits = mant;
    else
        bits = ((uint64_t) (target_exp - 1) << frac_bits) + mant;
    if (target_exp >= (int) exp_special || bits >= ((uint64_t) exp_special << frac_bits)) {
        // overflow: infinity, or the largest finite number when rounding toward zero
        bool to_inf = f80_rounding_mode == round_to_nearest || (f80_rounding_mode == round_up && !f.sign) || (f80_rounding_mode == round_down && f.sign);
        bits = ((uint64_t) exp_special << frac_bits) - (to_inf ? 0 : 1);
    }
    return sign | bits;
}

double f80_to_double(float80 f) {
    uint64_t bits = f80_to_ieee(f, 52, 11);
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

float f80_to_float(float80 f) {
    uint32_t bits = (uint32_t) f80_to_ieee(f, 23, 8);
    float d;
    memcpy(&d, &bits, sizeof(d));
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

// The x87 rule for NaN operands: the result is the NaN operand, made quiet;
// of two NaNs, the one with the larger significand (the positive one if they
// are equal).
static float80 f80_nan_operand(float80 a, float80 b) {
    float80 res;
    if (f80_isnan(a) && f80_isnan(b)) {
        uint64_t sa = a.signif | F80_QUIET_BIT, sb = b.signif | F80_QUIET_BIT;
        res = sa > sb ? a : sb > sa ? b : a.sign ? b : a;
    } else {
        res = f80_isnan(a) ? a : b;
    }
    res.signif |= F80_QUIET_BIT;
    return res;
}
#define handle_nans(a, b) do { \
    if (!f80_is_supported(a) || !f80_is_supported(b)) \
        return F80_NAN; \
    if (f80_isnan(a) || f80_isnan(b)) \
        return f80_nan_operand(a, b); \
} while(0)

// ---- arithmetic: unpack, compute exactly, round once ----

// A finite nonzero number as sig * 2^(exp - BIAS80 - 63), top bit of sig set.
// exp may be zero or negative for denormals.
struct f80_unpacked {
    uint64_t sig;
    int exp;
};
static struct f80_unpacked f80_unpack(float80 f) {
    struct f80_unpacked u;
    u.exp = f.exp == EXP_DENORMAL ? EXP_MIN : (int) f.exp;
    int lz = __builtin_clzl(f.signif);
    u.sig = f.signif << lz;
    u.exp -= lz;
    return u;
}

// Round and pack the value hi:lo * 2^(exp - BIAS80 - 127), where hi has its
// top bit set and bit 0 of lo may stand for any number of lower nonzero bits.
static float80 f80_round_pack(int sign, int exp, uint64_t hi, uint64_t lo) {
    if (exp < EXP_MIN) {
        // too small for a normal number: line it up with the smallest exponent
        int shift = EXP_MIN - exp;
        if (shift >= 128) {
            lo = 1;
            hi = 0;
        } else if (shift > 64) {
            lo = (hi >> (shift - 64)) | (((hi << (128 - shift)) | lo) != 0);
            hi = 0;
        } else if (shift == 64) {
            lo = hi | (lo != 0);
            hi = 0;
        } else {
            uint64_t out = lo << (64 - shift);
            lo = (hi << (64 - shift)) | (lo >> shift) | (out != 0);
            hi >>= shift;
        }
        exp = EXP_DENORMAL;
    }
    bool guard = lo >> 63, sticky = (lo << 1) != 0, inexact = lo != 0;
    bool up = false;
    switch (f80_rounding_mode) {
        case round_to_nearest: up = guard && (sticky || (hi & 1)); break;
        case round_down: up = sign && inexact; break;
        case round_up: up = !sign && inexact; break;
        case round_chop: break;
    }
    if (up && ++hi == 0) {
        hi = CURSED_BIT;
        exp++;
    }
    // a denormal that rounded up to the smallest normal number
    if (exp == EXP_DENORMAL && (hi >> 63))
        exp = EXP_MIN;
    if (exp > EXP_MAX) {
        bool to_inf = f80_rounding_mode == round_to_nearest || (f80_rounding_mode == round_up && !sign) || (f80_rounding_mode == round_down && sign);
        float80 f = to_inf ? F80_INF : (float80) {.exp = EXP_MAX, .signif = -1ul};
        f.sign = sign;
        return f;
    }
    return (float80) {.signif = hi, .exp = (unsigned) exp, .sign = (unsigned) sign};
}

static float80 f80_zero(int sign) {
    return (float80) {.signif = 0, .exp = 0, .sign = (unsigned) sign};
}

// a + b, or a - b
static float80 f80_addsub(float80 a, float80 b, bool subtract) {
    handle_nans(a, b);
    if (subtract)
        b.sign = !b.sign;
    if (f80_isinf(a) || f80_isinf(b)) {
        // infinity - infinity is invalid
        if (f80_isinf(a) && f80_isinf(b) && a.sign != b.sign)
            return F80_NAN;
        return f80_isinf(a) ? a : b;
    }
    if (b.signif == 0) {
        if (a.signif != 0)
            return a;
        // zero + zero: the common sign, else +0 (-0 when rounding down)
        return f80_zero(a.sign == b.sign ? a.sign : f80_rounding_mode == round_down);
    }
    if (a.signif == 0)
        return b;

    struct f80_unpacked ua = f80_unpack(a), ub = f80_unpack(b);
    int sign = a.sign, sign_b = b.sign;
    if (ua.exp < ub.exp || (ua.exp == ub.exp && ua.sig < ub.sig)) {
        struct f80_unpacked tmp = ua; ua = ub; ub = tmp;
        sign = b.sign; sign_b = a.sign;
    }
    // line b up with a as a 128-bit value, low bit sticky
    int d = ua.exp - ub.exp;
    uint64_t bhi, blo;
    if (d == 0) {
        bhi = ub.sig; blo = 0;
    } else if (d < 64) {
        bhi = ub.sig >> d; blo = ub.sig << (64 - d);
    } else if (d == 64) {
        bhi = 0; blo = ub.sig;
    } else if (d < 128) {
        bhi = 0; blo = (ub.sig >> (d - 64)) | ((ub.sig << (128 - d)) != 0);
    } else {
        bhi = 0; blo = 1;
    }
    uint128_t x = (uint128_t) ua.sig << 64, y = ((uint128_t) bhi << 64) | blo, r;
    int exp = ua.exp;
    if (sign == sign_b) {
        if (__builtin_add_overflow(x, y, &r)) {
            // carry out: one bit to the right, keeping what falls off as sticky
            r = (r >> 1) | (r & 1) | ((uint128_t) 1 << 127);
            exp++;
        }
    } else {
        r = x - y; // x >= y by the ordering above
        if (r == 0)
            return f80_zero(f80_rounding_mode == round_down);
        int lz = u128_clz(r);
        r <<= lz;
        exp -= lz;
    }
    return f80_round_pack(sign, exp, (uint64_t) (r >> 64), (uint64_t) r);
}

float80 f80_add(float80 a, float80 b) {
    return f80_addsub(a, b, false);
}

float80 f80_sub(float80 a, float80 b) {
    return f80_addsub(a, b, true);
}

float80 f80_mul(float80 a, float80 b) {
    handle_nans(a, b);
    int sign = a.sign ^ b.sign;
    if (f80_isinf(a) || f80_isinf(b)) {
        // infinity times zero is invalid
        if (a.signif == 0 || b.signif == 0)
            return F80_NAN;
        float80 f = F80_INF;
        f.sign = sign;
        return f;
    }
    if (a.signif == 0 || b.signif == 0)
        return f80_zero(sign);

    struct f80_unpacked ua = f80_unpack(a), ub = f80_unpack(b);
    uint128_t p = (uint128_t) ua.sig * ub.sig; // top bit is bit 127 or 126
    int exp = ua.exp + ub.exp - BIAS80 + 1;
    if (!(p >> 127)) {
        p <<= 1;
        exp--;
    }
    return f80_round_pack(sign, exp, (uint64_t) (p >> 64), (uint64_t) p);
}

float80 f80_div(float80 a, float80 b) {
    handle_nans(a, b);
    int sign = a.sign ^ b.sign;
    if (f80_isinf(a)) {
        // infinity / infinity is invalid
        if (f80_isinf(b))
            return F80_NAN;
        float80 f = F80_INF;
        f.sign = sign;
        return f;
    }
    if (f80_isinf(b))
        return f80_zero(sign);
    if (b.signif == 0) {
        // 0 / 0 is invalid, anything else / 0 is infinity
        if (a.signif == 0)
            return F80_NAN;
        float80 f = F80_INF;
        f.sign = sign;
        return f;
    }
    if (a.signif == 0)
        return f80_zero(sign);

    struct f80_unpacked ua = f80_unpack(a), ub = f80_unpack(b);
    // choose the numerator so the quotient has exactly 64 bits
    uint128_t n;
    int exp = ua.exp - ub.exp + BIAS80;
    if (ua.sig < ub.sig) {
        n = (uint128_t) ua.sig << 64;
        exp--;
    } else {
        n = (uint128_t) ua.sig << 63;
    }
    uint64_t q = (uint64_t) (n / ub.sig);
    uint64_t rem = (uint64_t) (n % ub.sig);
    // 64 more bits of the quotient, and whether anything is left after them
    uint128_t n2 = (uint128_t) rem << 64;
    uint64_t lo = (uint64_t) (n2 / ub.sig);
    if (n2 % ub.sig != 0)
        lo |= 1;
    return f80_round_pack(sign, exp, q, lo);
}

// x - trunc(x/y)*y, exactly (what fprem computes once it has finished).
// Done on the significands as integers: no intermediate rounding.
float80 f80_mod(float80 x, float80 y) {
    handle_nans(x, y);
    if (f80_isinf(x) || y.signif == 0)
        return F80_NAN;
    if (f80_isinf(y) || x.signif == 0)
        return x;
    struct f80_unpacked ux = f80_unpack(x), uy = f80_unpack(y);
    if (ux.exp < uy.exp)
        return x;
    uint64_t r = ux.sig % uy.sig;
    for (int d = ux.exp - uy.exp; d > 0 && r != 0;) {
        int k = d > 62 ? 62 : d;
        r = (uint64_t) (((uint128_t) r << k) % uy.sig);
        d -= k;
    }
    if (r == 0)
        return f80_zero(x.sign);
    int lz = __builtin_clzl(r);
    // exact, so nothing is rounded: this only packs (denormals included)
    return f80_round_pack(x.sign, uy.exp - lz, r << lz, 0);
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

// Correctly rounded square root: integer square root of the significand.
float80 f80_sqrt(float80 x) {
    if (f80_iszero(x))
        return x;
    if (!f80_is_supported(x))
        return F80_NAN;
    if (f80_isnan(x))
        return f80_nan_operand(x, x);
    if (x.sign)
        return F80_NAN;
    if (f80_isinf(x))
        return x;
    // x = m * 2^e with m a 64-bit integer whose top bit is set
    uint64_t m = x.signif;
    int e = (x.exp ? (int) x.exp : 1) - 16383 - 63;
    while (!(m >> 63)) { m <<= 1; e--; }
    // a = m * 2^shift with shift chosen so that (e - shift) is even and the
    // root has exactly 64 bits
    int shift = (e & 1) ? 63 : 64;
    uint128_t a = (uint128_t) m << shift;
    // floor(sqrt(a)): a double-precision estimate, one Newton step in integers
    // (which leaves it within 1), then an exact correction
    double estimate = __builtin_sqrt((double) (uint64_t) (a >> 64)) * 4294967296.0;
    uint64_t r0 = estimate >= 18446744073709549568.0 ? ~(uint64_t) 0 : (uint64_t) estimate;
    if (r0 < ((uint64_t) 1 << 63))
        r0 = (uint64_t) 1 << 63;
    uint128_t root = ((uint128_t) r0 + a / r0) >> 1;
    if (root >> 64)
        root = ~(uint64_t) 0;
    while (root * root > a)
        root--;
    while (root != ~(uint64_t) 0 && (root + 1) * (root + 1) <= a)
        root++;
    uint128_t rem = a - root * root;
    // root = floor(sqrt(a)), rem = a - root^2. sqrt(a) is never exactly
    // halfway between two integers, so rounding to nearest is "rem > root".
    bool up = false;
    if (f80_rounding_mode == round_to_nearest)
        up = rem > root;
    else if (f80_rounding_mode == round_up)
        up = rem != 0;
    int exp = (e - shift) / 2 + 63;
    uint64_t r = (uint64_t) root;
    if (up && ++r == 0) {
        r = (uint64_t) 1 << 63;
        exp++;
    }
    return (float80) {.signif = r, .exp = (unsigned) (exp + 16383), .sign = 0};
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
