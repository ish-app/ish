#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include "float80.h"

//#define DENORMAL 1e-310
#define DENORMAL 1.11253692925360069155e-308

union f80 {
    float80 f;
    long double ld;
};

union f80 gf;

static bool bitwise_eq(long double a, long double b) {
    union f80 ua = (union f80) a;
    union f80 ub = (union f80) b;
    return ua.f.signif == ub.f.signif && ua.f.signExp == ub.f.signExp;
}

static int tests_passed = 0;
static int tests_total = 0;
static int suite_passed = 0;
static int suite_total = 0;

#define suite_start() _suite_start(__FUNCTION__)
#define suite_end() _suite_end(__FUNCTION__)
void _suite_start(const char *suite) {
    printf("==== %s ====\n", suite);
    suite_passed = 0;
    suite_total = 0;
}
void _suite_end(const char *suite) {
    printf("%s: %d/%d passed (%.0f%%)\n", suite, suite_passed, suite_total, (double) suite_passed / suite_total * 100);
}

void assertf(int cond, const char *msg, ...) {
    tests_total++;
    suite_total++;
    if (cond) {
        tests_passed++;
        suite_passed++;
    }

    printf(cond ? "PASS ": "FAIL ");
    char buf[1024];
    va_list args;
    va_start(args, msg);
    vsprintf(buf, msg, args);
    va_end(args);
    puts(buf);
}

void test_int_convert() {
    suite_start();
    union f80 u;
    int64_t i;
#define test(x) \
    u.f = f80_from_int(x); \
    assertf((int64_t) u.ld == x, "f80_from_int(%ld) = %.20Le", (int64_t) x, u.ld); \
    i = f80_to_int(u.f); \
    assertf(i == x, "f80_to_int(%.20Le) = %ld", u.ld, i)

    test(0);
    test(123); test(-123);
    test(9489919999192); test(-9489919999192);
    test(INT64_MIN); test(INT64_MAX);
#undef test

#define test(x) \
    u.f = f80_from_double(x); \
    i = f80_to_int(u.f); \
    assertf(i == (int64_t)round(x), "f80_to_int(f80_from_double(%.20Le)) = %ld", u.ld, i)

    test(0.75); test(-0.75);
#undef test

    suite_end();
}

void test_double_convert() {
    suite_start();
    union f80 u;
    double d;
#define test(x) \
    u.f = f80_from_double(x); \
    assertf(bitwise_eq(u.ld, x), "f80_from_double(%e) = %Le", (double) x, u.ld); \
    d = f80_to_double(u.f); \
    assertf(bitwise_eq(d, x), "f80_to_double(%Le) = %e", u.ld, d)

    test(0); test(-0);
    test(123); test(-123);
    test(3991994929919994995881.0);
    test(9.223372036854776e18);
    test(DENORMAL);
    test(1e-310);
    test(INFINITY); test(-INFINITY);
    test(NAN);
#undef test
    suite_end();
}

void test_math() {
    suite_start();
    union f80 ua, ub, u;
    long double expected;
#define cop_add +
#define cop_sub -
#define cop_mul *
#define cop_div /
#define _test(op, a, b) \
    ua.ld = a; ub.ld = b; \
    u.f = f80_##op(ua.f, ub.f); \
    expected = (long double) a cop_##op (long double) b; \
    assertf(bitwise_eq(u.ld, expected), "f80_"#op"(%Le, %Le) = %Le (%Le)", ua.ld, ub.ld, u.ld, expected)
#define test(op, a, b) \
    _test(op, a, b); \
    _test(op, -a, b); \
    _test(op, a, -b); \
    _test(op, -a, -b)

    test(add, 1, 1);
    test(add, 123, 123);
    test(add, 9942, 13459);
    test(add, 222, 0.);
    test(add, 0., 0.);
    test(add, 12.0499, 91999);
    test(add, 1e100, 100);
    test(add, 1e-4949l, 1);
    test(add, 1e-4949l, 1e-4949l);
    test(add, 1e-4949l, 2e-4949l);
    test(add, 18446744073709551616.l, 1.5);
    test(add, INFINITY, 1);
    test(add, INFINITY, 123);
    test(add, INFINITY, INFINITY);
    test(add, NAN, 123);
    test(add, NAN, NAN);

    test(mul, 0, 1);
    test(mul, 1, 1);
    test(mul, 123, 123);
    test(mul, 1e100, 100);
    test(mul, 12.3993l, 91934);
    test(mul, 1e-4949l, 1);
    test(mul, 1e-4949l, 1e-4949l);
    test(mul, INFINITY, 11);
    test(mul, INFINITY, INFINITY);
    test(mul, INFINITY, 0);
    test(mul, NAN, 123);
    test(mul, NAN, NAN);

    test(div, 1, 1);
    test(div, 3, 2);
    test(div, 0, 1);
    test(div, 0, 0);
    test(div, 1, 1e1000l);
    test(div, 1, 1e-1000l);
    test(div, 12.4123331, 934.11223e200);
    test(div, 1288490188200, 210);
    test(div, 1e-4949l, 10);
    test(div, 10, 1e-4949l);
    test(div, 1e-4949l, 1e-4949l);
    test(div, INFINITY, 100);
    test(div, 100, INFINITY);
    test(div, INFINITY, INFINITY);
    test(div, INFINITY, 0);
    test(div, NAN, 123);
    test(div, NAN, NAN);
#undef test
#undef _test
    suite_end();
}

void test_compare() {
    suite_start();
    union f80 ua, ub;
    bool expected, actual;
#define cop_eq ==
#define cop_lt <
#define _test(op, a, b) \
    ua.ld = a; ub.ld = b; \
    actual = f80_##op(ua.f, ub.f); \
    expected = (long double) a cop_##op (long double) b; \
    assertf(actual == expected, "f80_"#op"(%Le, %Le) = %s", ua.ld, ub.ld, actual ? "true" : "false")
#define test(op, a, b) \
    _test(op, a, b); \
    _test(op, -a, b); \
    _test(op, a, -b); \
    _test(op, -a, -b)

    test(eq, 0, 0);
    test(eq, 1, 1);
    test(eq, 0, 1);
    test(eq, INFINITY, INFINITY);
    test(eq, 1, INFINITY);
    test(eq, NAN, 123);
    test(eq, NAN, NAN);
    test(lt, 0, 0);
    test(lt, 1, 1);
    test(lt, 0, 1);
    test(lt, INFINITY, INFINITY);
    test(lt, 1, INFINITY);
    test(lt, NAN, 123);
    test(lt, NAN, NAN);
#undef test
    suite_end();
}

uint64_t fnmulh(uint64_t a, uint64_t b) {
    return ((unsigned __int128) a * b) >> 64;
}

int main() {
    test_int_convert();
    test_double_convert();
    test_math();
    test_compare();
    printf("%d/%d passed (%.0f%%)", tests_passed, tests_total, (double) tests_passed / tests_total * 100);
    return tests_passed == tests_total ? 0 : 1;
}
