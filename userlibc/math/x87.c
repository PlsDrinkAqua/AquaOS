#include <math.h>
#include <stdint.h>
#include <float.h>
#include <errno.h>
#if LDBL_MANT_DIG != 64 || LDBL_MAX_EXP != 16384
#error This backend requires x86 80-bit long double
#endif
union extended { long double value; struct { uint64_t significand; uint16_t exponent; } bits; };
long double frexpl(long double x, int *exponent) {
    union extended u = { .value = x };
    unsigned e = u.bits.exponent & 0x7fff;
    *exponent = 0;
    if (e == 0x7fff || x == 0) return x;
    if (!e) {
        u.value = x * 0x1p64L;
        e = u.bits.exponent & 0x7fff;
        *exponent = -64;
    }
    *exponent += (int)e - 0x3ffe;
    u.bits.exponent = (u.bits.exponent & 0x8000) | 0x3ffe;
    return u.value;
}
long double scalbnl(long double x, int exponent) {
    long double result;
    __asm__ ("fscale; fstp %%st(1)" : "=t"(result) : "0"(x), "u"((long double)exponent) : "st(1)");
    return result;
}
double scalbn(double x, int exponent) { return (double)scalbnl(x, exponent); }
double ldexp(double x, int exponent) {
    double result = scalbn(x, exponent);
    if (isfinite(x) && x != 0 && (!isfinite(result) || result == 0)) errno = ERANGE;
    return result;
}
long double fabsl(long double x) {
    union extended u = { .value = x }; u.bits.exponent &= 0x7fff; return u.value;
}
long double copysignl(long double x, long double sign) {
    union extended u = { .value = x }, s = { .value = sign };
    u.bits.exponent = (u.bits.exponent & 0x7fff) | (s.bits.exponent & 0x8000);
    return u.value;
}
long double fmodl(long double x, long double y) {
    long double result;
    if (y == 0 || isinf(x)) errno = EDOM;
    __asm__ ("1: fprem; fnstsw %%ax; test $0x400, %%ax; jnz 1b; fstp %%st(1)"
             : "=t"(result) : "0"(x), "u"(y) : "ax", "cc", "st(1)");
    return result;
}
