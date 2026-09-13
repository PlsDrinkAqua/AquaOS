#ifndef _USERLIBC_MATH_H
#define _USERLIBC_MATH_H 1
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#ifdef __cplusplus
extern "C" {
#endif
long double frexpl(long double x, int *exponent);
long double scalbnl(long double x, int exponent);
double scalbn(double x, int exponent);
double ldexp(double x, int exponent);
long double fabsl(long double x);
long double copysignl(long double x, long double sign);
long double fmodl(long double x, long double y);
#ifdef __cplusplus
}
#endif
#endif
