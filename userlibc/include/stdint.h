#ifndef _USERLIBC_STDINT_H
#define _USERLIBC_STDINT_H
/* AquaOS x86-64 and the LP64 host used by libc tests. */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
#ifdef __LP64__
typedef long int64_t;
typedef unsigned long uint64_t;
#define INT64_C(x) x##L
#define UINT64_C(x) x##UL
#else
typedef long long int64_t;
typedef unsigned long long uint64_t;
#define INT64_C(x) x##LL
#define UINT64_C(x) x##ULL
#endif
typedef __PTRDIFF_TYPE__ intptr_t;
typedef __SIZE_TYPE__ uintptr_t;
typedef int64_t intmax_t;
typedef uint64_t uintmax_t;
#define INT8_MIN (-127-1)
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN (-32767-1)
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT32_MIN (-2147483647-1)
#define INT32_MAX 2147483647
#define UINT32_MAX 4294967295U
#define INT64_MAX INT64_C(9223372036854775807)
#define INT64_MIN (-INT64_MAX-1)
#define UINT64_MAX UINT64_C(18446744073709551615)
#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX
#define UINTMAX_MAX UINT64_MAX
#ifdef __LP64__
#define INTPTR_MIN INT64_MIN
#define INTPTR_MAX INT64_MAX
#define UINTPTR_MAX UINT64_MAX
#else
#define INTPTR_MIN INT32_MIN
#define INTPTR_MAX INT32_MAX
#define UINTPTR_MAX UINT32_MAX
#endif
#define SIZE_MAX UINTPTR_MAX
#define PTRDIFF_MIN INTPTR_MIN
#define PTRDIFF_MAX INTPTR_MAX
#define INT8_C(x) x
#define UINT8_C(x) x
#define INT16_C(x) x
#define UINT16_C(x) x
#define INT32_C(x) x
#define UINT32_C(x) x##U
#define INTMAX_C(x) INT64_C(x)
#define UINTMAX_C(x) UINT64_C(x)
typedef int8_t int_least8_t;
typedef uint8_t uint_least8_t;
#define INT_LEAST8_MIN INT8_MIN
#define INT_LEAST8_MAX INT8_MAX
#define UINT_LEAST8_MAX UINT8_MAX
typedef int16_t int_least16_t;
typedef uint16_t uint_least16_t;
#define INT_LEAST16_MIN INT16_MIN
#define INT_LEAST16_MAX INT16_MAX
#define UINT_LEAST16_MAX UINT16_MAX
typedef int32_t int_least32_t;
typedef uint32_t uint_least32_t;
#define INT_LEAST32_MIN INT32_MIN
#define INT_LEAST32_MAX INT32_MAX
#define UINT_LEAST32_MAX UINT32_MAX
typedef int64_t int_least64_t;
typedef uint64_t uint_least64_t;
#define INT_LEAST64_MIN INT64_MIN
#define INT_LEAST64_MAX INT64_MAX
#define UINT_LEAST64_MAX UINT64_MAX
typedef int8_t int_fast8_t;
typedef uint8_t uint_fast8_t;
#define INT_FAST8_MIN INT8_MIN
#define INT_FAST8_MAX INT8_MAX
#define UINT_FAST8_MAX UINT8_MAX
typedef int16_t int_fast16_t;
typedef uint16_t uint_fast16_t;
#define INT_FAST16_MIN INT16_MIN
#define INT_FAST16_MAX INT16_MAX
#define UINT_FAST16_MAX UINT16_MAX
typedef int32_t int_fast32_t;
typedef uint32_t uint_fast32_t;
#define INT_FAST32_MIN INT32_MIN
#define INT_FAST32_MAX INT32_MAX
#define UINT_FAST32_MAX UINT32_MAX
typedef int64_t int_fast64_t;
typedef uint64_t uint_fast64_t;
#define INT_FAST64_MIN INT64_MIN
#define INT_FAST64_MAX INT64_MAX
#define UINT_FAST64_MAX UINT64_MAX
#endif
