#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static int digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 36;
}

/* C99-style integer syntax. No locale or C23 binary-prefix extension. */
static unsigned long long convert(const char *text, char **end, int base,
                                   unsigned long long positive_limit,
                                   unsigned long long negative_limit,
                                   int *negative, int *overflow) {
    const char *p = text;
    unsigned long long value = 0;
    *negative = *overflow = 0;
    if (end) *end = (char *)text;
    if (base && (base < 2 || base > 36)) { errno = EINVAL; return 0; }
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    if (*p == '-' || *p == '+') *negative = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' &&
        (p[1] == 'x' || p[1] == 'X') && digit((unsigned char)p[2]) < 16) {
        p += 2; base = 16;
    }
    if (!base) base = *p == '0' ? 8 : 10;
    const char *start = p;
    unsigned long long limit = *negative ? negative_limit : positive_limit;
    unsigned long long cutoff = limit / (unsigned)base;
    unsigned cutlim = limit % (unsigned)base;
    for (;;) {
        unsigned d = digit((unsigned char)*p);
        if (d >= (unsigned)base) break;
        if (value > cutoff || (value == cutoff && d > cutlim)) *overflow = 1;
        if (!*overflow) value = value * (unsigned)base + d;
        p++;
    }
    if (p != start && end) *end = (char *)p;
    if (*overflow) { errno = ERANGE; return limit; }
    return value;
}

long strtol(const char *text, char **end, int base) {
    int negative, overflow;
    unsigned long long value = convert(text, end, base, LONG_MAX,
                                       (unsigned long long)LONG_MAX + 1, &negative, &overflow);
    if (overflow) return negative ? LONG_MIN : LONG_MAX;
    if (negative && value == (unsigned long long)LONG_MAX + 1) return LONG_MIN;
    return negative ? -(long)value : (long)value;
}

unsigned long strtoul(const char *text, char **end, int base) {
    int negative, overflow;
    unsigned long long value = convert(text, end, base, ULONG_MAX, ULONG_MAX, &negative, &overflow);
    if (overflow) return ULONG_MAX;
    return negative ? 0UL - (unsigned long)value : (unsigned long)value;
}

long long strtoll(const char *text, char **end, int base) {
    int negative, overflow;
    unsigned long long value = convert(text, end, base, LLONG_MAX,
                                       (unsigned long long)LLONG_MAX + 1, &negative, &overflow);
    if (overflow) return negative ? LLONG_MIN : LLONG_MAX;
    if (negative && value == (unsigned long long)LLONG_MAX + 1) return LLONG_MIN;
    return negative ? -(long long)value : (long long)value;
}

unsigned long long strtoull(const char *text, char **end, int base) {
    int negative, overflow;
    unsigned long long value = convert(text, end, base, ULLONG_MAX, ULLONG_MAX, &negative, &overflow);
    if (overflow) return ULLONG_MAX;
    return negative ? 0ULL - value : value;
}

int atoi(const char *text) { return (int)strtol(text, NULL, 10); }
