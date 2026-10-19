#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include "string_scan.h"

enum length { NORMAL, HH, H, L, LL, Z, T, J, BIGL };

/* Store through the destination type selected by the format. */
static void store_integer(void *out, enum length length, uintmax_t value, int sign) {
    if (sign) {
        switch (length) {
        case HH: *(signed char *)out = value; break;
        case H: *(short *)out = value; break;
        case L: *(long *)out = value; break;
        case LL: *(long long *)out = value; break;
        case Z: case T: *(ptrdiff_t *)out = value; break;
        case J: *(intmax_t *)out = value; break;
        default: *(int *)out = value; break;
        }
    } else {
        switch (length) {
        case HH: *(unsigned char *)out = value; break;
        case H: *(unsigned short *)out = value; break;
        case L: *(unsigned long *)out = value; break;
        case LL: *(unsigned long long *)out = value; break;
        case Z: *(size_t *)out = value; break;
        case T: *(uintptr_t *)out = value; break;
        case J: *(uintmax_t *)out = value; break;
        default: *(unsigned *)out = value; break;
        }
    }
}

static int digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int integer(string_scan *input, int base, uintmax_t *value) {
    int c = shgetc(input), neg = 0, digits = 0, overflow = 0;
    uintmax_t n = 0;
    if (c == '+' || c == '-') { neg = c == '-'; c = shgetc(input); }
    if (c == '0') {
        digits = 1;
        c = shgetc(input);
        if ((base == 0 || base == 16) && (c == 'x' || c == 'X')) {
            base = 16; digits = 0; c = shgetc(input);
        } else if (!base) base = 8;
    }
    if (!base) base = 10;
    int d;
    while ((d = digit(c)) >= 0 && d < base) {
        digits = 1;
        if (n > (UINTMAX_MAX - (unsigned)d) / (unsigned)base) overflow = 1;
        else n = n * (unsigned)base + (unsigned)d;
        c = shgetc(input);
    }
    shunget(input);
    if (overflow) { errno = ERANGE; n = UINTMAX_MAX; }
    *value = neg && !overflow ? -n : n;
    return digits;
}

int vsscanf(const char *text, const char *format, va_list args) {
    const unsigned char *start = (const unsigned char *)text, *p = start;
    int assigned = 0;
    while (*format) {
        if (isspace((unsigned char)*format)) {
            do format++; while (isspace((unsigned char)*format));
            while (isspace(*p)) p++;
            continue;
        }
        if (*format != '%' || format[1] == '%') {
            if (*format == '%') format++;
            if (!*p) goto input_failure;
            if (*p != (unsigned char)*format) return assigned;
            p++; format++; continue;
        }
        format++;
        int suppress = *format == '*';
        if (suppress) format++;
        size_t width = 0;
        int has_width = 0;
        while (*format >= '0' && *format <= '9') {
            unsigned d = *format++ - '0'; has_width = 1;
            if (width > (SIZE_MAX - d) / 10) goto invalid;
            width = width * 10 + d;
        }
        if (has_width && !width) goto invalid;
        enum length length = NORMAL;
        switch (*format) {
        case 'h': format++; length = H; if (*format == 'h') { format++; length = HH; } break;
        case 'l': format++; length = L; if (*format == 'l') { format++; length = LL; } break;
        case 'j': format++; length = J; break;
        case 'z': format++; length = Z; break;
        case 't': format++; length = T; break;
        case 'L': format++; length = BIGL; break;
        }
        int spec = (unsigned char)*format;
        if (!spec) goto invalid;
        format++;
        if (spec == 'n') {
            if (length == BIGL || suppress || has_width) goto invalid;
            store_integer(va_arg(args, void *), length, (size_t)(p-start), 1);
            continue;
        }
        unsigned char set[256] = {0};
        int inverted = 0;
        if (spec == '[') {
            if (*format == '^') { inverted = 1; format++; }
            if (*format == ']') { set[']'] = 1; format++; }
            while (*format && *format != ']') {
                unsigned first = (unsigned char)*format++;
                if (*format == '-' && format[1] && format[1] != ']' &&
                    first <= (unsigned char)format[1]) {
                    unsigned last = (unsigned char)format[1]; format += 2;
                    for (unsigned i = first; i <= last; i++) set[i] = 1;
                } else set[first] = 1;
            }
            if (*format != ']') goto invalid;
            format++;
        }
        int floating = spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E' ||
                       spec == 'g' || spec == 'G' || spec == 'a' || spec == 'A';
        int number = spec == 'd' || spec == 'i' || spec == 'u' || spec == 'o' ||
                     spec == 'x' || spec == 'X' || spec == 'p';
        if (floating ? (length != NORMAL && length != L && length != BIGL) :
            number ? (length == BIGL || (spec == 'p' && length != NORMAL)) :
            ((spec != 's' && spec != 'c' && spec != '[') || length != NORMAL)) goto invalid;
        if (spec != 'c' && spec != '[') while (isspace(*p)) p++;
        if (!*p) goto input_failure;
        if (!has_width) width = spec == 'c' ? 1 : SIZE_MAX;
        void *out = suppress ? NULL : va_arg(args, void *);
        if (floating || number) {
            string_scan input = { p, 0, width, 0, 0 };
            if (floating) {
                long double value = __aquaos_floatscan(&input, length == BIGL ? 2 : length == L ? 1 : 0, 0);
                if (input.failed) return assigned;
                if (out) {
                    if (length == BIGL) *(long double *)out = value;
                    else if (length == L) *(double *)out = value;
                    else *(float *)out = value;
                }
            } else {
                int base = spec == 'i' ? 0 : spec == 'o' ? 8 :
                           spec == 'x' || spec == 'X' || spec == 'p' ? 16 : 10;
                uintmax_t value;
                if (!integer(&input, base, &value)) return assigned;
                if (out) {
                    if (spec == 'p') *(void **)out = (void *)(uintptr_t)value;
                    else store_integer(out, length, value, spec == 'd' || spec == 'i');
                }
            }
            p += input.pos;
        } else {
            size_t n = 0;
            while (n < width && p[n] && (spec == 'c' ||
                   (spec == 's' ? !isspace(p[n]) : !!set[p[n]] != inverted))) {
                if (out) ((char *)out)[n] = p[n];
                n++;
            }
            if (spec == 'c' && n < width) goto input_failure;
            if (!n) return assigned;
            if (out && spec != 'c') ((char *)out)[n] = 0;
            p += n;
        }
        if (!suppress) assigned++;
    }
    return assigned;
input_failure:
    return assigned ? assigned : EOF;
invalid:
    errno = EINVAL;
    return assigned ? assigned : EOF;
}

int sscanf(const char *text, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsscanf(text, format, args);
    va_end(args);
    return result;
}
