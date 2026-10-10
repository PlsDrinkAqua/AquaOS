#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <limits.h>
#include <float.h>
#ifndef __zenos__
#error Missing ZenOS target macro
#endif
#define CHECK(x) do { if (!(x)) { printf("FAIL native line %d\n", __LINE__); return 1; } } while (0)
static jmp_buf env;
static void jump(int value) { longjmp(env, value); }
static int sum(int n, ...) {
    va_list a; int result = 0; va_start(a, n);
    while (n--) result += va_arg(a, int);
    va_end(a); return result;
}
extern int companion(int);
int main(void) {
    char text[128]; int n; double d;
    CHECK(stdout != NULL && stderr != NULL);
    volatile unsigned long long large = 100000000000ULL, divisor = 7;
    CHECK(large / divisor == 14285714285ULL && large % divisor == 5);
    CHECK(sizeof(long double) == 16 && sizeof(void *) == 8 && sizeof(long) == 8);
    CHECK(INT_MAX == 2147483647);
    CHECK(sum(3, 7, 11, 13) == 31 && companion(21) == 42);
    CHECK(sscanf("42 1.25", "%d %lf", &n, &d) == 2 && n == 42 && d == 1.25);
    CHECK(snprintf(text, sizeof(text), "%.2f %.3Lf", d * 2, 1.125L) == 10);
    CHECK(!strcmp(text, "2.50 1.125"));
    CHECK(strtod("0x1.8p2", NULL) == 6);
    volatile int stage = 0;
    switch (setjmp(env)) {
        case 0: stage = 1; jump(0); break;
        case 1: CHECK(stage == 1); stage = 2; jump(37); break;
        case 37: CHECK(stage == 2); break;
        default: CHECK(0);
    }
    CHECK(!strcmp(__DATE__, "Jan  1 1970") && !strcmp(__TIME__, "00:00:00"));
    puts("native compiler checks passed");
    return 0;
}
