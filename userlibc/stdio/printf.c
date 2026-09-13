#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <string.h>

typedef struct { FILE *file; char *buffer; size_t capacity, count; int error; } output_t;
static void emit(output_t *out, const char *text, size_t size) {
    if (out->error) return;
    if (size > (size_t)INT_MAX - out->count) { errno = EOVERFLOW; out->error = 1; return; }
    if (out->file) {
        if (fwrite(text, 1, size, out->file) != size) { out->error = 1; return; }
    } else if (out->capacity && out->count < out->capacity - 1) {
        size_t copy = out->capacity - 1 - out->count;
        if (copy > size) copy = size;
        memcpy(out->buffer + out->count, text, copy);
    }
    out->count += size;
}
static void pad(output_t *out, char c, size_t size) {
    if (out->error) return;
    if (size > (size_t)INT_MAX - out->count) { errno = EOVERFLOW; out->error = 1; return; }
    if (!out->file) {
        if (out->capacity && out->count < out->capacity - 1) {
            size_t copy = out->capacity - 1 - out->count;
            if (copy > size) copy = size;
            memset(out->buffer + out->count, c, copy);
        }
        out->count += size;
    } else {
        char block[64]; memset(block, c, sizeof(block));
        while (size && !out->error) {
            size_t n = size < sizeof(block) ? size : sizeof(block);
            emit(out, block, n); size -= n;
        }
    }
}
static int decimal(const char **format, int *value) {
    *value = 0;
    while (**format >= '0' && **format <= '9') {
        int d = *(*format)++ - '0';
        if (*value > (INT_MAX - d) / 10) return -1;
        *value = *value * 10 + d;
    }
    return 0;
}
#include "float_format.inc"
enum { NORMAL, HH, H, L, LL, Z, T, J, LONG_DOUBLE };

static int format(output_t *out, const char *fmt, va_list incoming) {
    va_list args; va_copy(args, incoming);
    while (*fmt && !out->error) {
        if (*fmt != '%') {
            const char *start = fmt;
            while (*fmt && *fmt != '%') fmt++;
            emit(out, start, fmt - start); continue;
        }
        fmt++;
        int left = 0, plus = 0, space = 0, alternate = 0, zero = 0;
        for (;;) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '+') plus = 1;
            else if (*fmt == ' ') space = 1;
            else if (*fmt == '#') alternate = 1;
            else if (*fmt == '0') zero = 1;
            else break;
            fmt++;
        }
        int width = 0, precision = -1, length = NORMAL;
        if (*fmt == '*') {
            fmt++; width = va_arg(args, int);
            if (width == INT_MIN) goto overflow;
            if (width < 0) { left = 1; width = -width; }
        } else if (decimal(&fmt, &width) < 0) goto overflow;
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') { fmt++; precision = va_arg(args, int); if (precision < 0) precision = -1; }
            else if (decimal(&fmt, &precision) < 0) goto overflow;
        }
        if (*fmt == 'h') { fmt++; length = H; if (*fmt == 'h') { fmt++; length = HH; } }
        else if (*fmt == 'l') { fmt++; length = L; if (*fmt == 'l') { fmt++; length = LL; } }
        else if (*fmt == 'L') { fmt++; length = LONG_DOUBLE; }
        else if (*fmt == 'z') { fmt++; length = Z; }
        else if (*fmt == 't') { fmt++; length = T; }
        else if (*fmt == 'j') { fmt++; length = J; }
        char spec = *fmt;
        if (!spec) goto unsupported;
        fmt++;
        if (spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E' ||
            spec == 'g' || spec == 'G' || spec == 'a' || spec == 'A') {
            if (length != NORMAL && length != L && length != LONG_DOUBLE) goto unsupported;
            long double value = length == LONG_DOUBLE ? va_arg(args, long double) : va_arg(args, double);
            int flags = (left ? LEFT_ADJ : 0) | (plus ? MARK_POS : 0) | (space ? PAD_POS : 0) |
                        (alternate ? ALT_FORM : 0) | (zero ? ZERO_PAD : 0);
            if (fmt_fp(out, value, width, precision, flags, spec, length == LONG_DOUBLE) < 0) goto overflow;
            continue;
        }
        if (length == LONG_DOUBLE) goto unsupported;
        if (spec == 's' || spec == 'c' || spec == '%') {
            if (length != NORMAL) goto unsupported;
            char c;
            const char *text;
            size_t n;
            if (spec == 's') {
                text = va_arg(args, const char *);
                if (!text) text = "(null)";
                n = 0;
                while ((precision < 0 || n < (size_t)precision) && text[n]) n++;
            } else {
                c = spec == '%' ? '%' : (char)va_arg(args, int);
                text = &c; n = 1;
            }
            size_t spaces = (size_t)width > n ? (size_t)width - n : 0;
            if (!left) pad(out, ' ', spaces);
            emit(out, text, n);
            if (left) pad(out, ' ', spaces);
            continue;
        }
        if (spec != 'd' && spec != 'i' && spec != 'u' && spec != 'o' &&
            spec != 'x' && spec != 'X' && spec != 'p') goto unsupported;
        unsigned long long value;
        char prefix[2]; size_t prefix_size = 0;
        int is_signed = spec == 'd' || spec == 'i';
        if (spec == 'p') {
            if (length != NORMAL) goto unsupported;
            value = (uintptr_t)va_arg(args, void *);
        } else if (is_signed) {
            long long v;
            switch (length) {
            case HH: v = (signed char)va_arg(args, int); break;
            case H: v = (short)va_arg(args, int); break;
            case L: v = va_arg(args, long); break;
            case LL: v = va_arg(args, long long); break;
            case Z: case T: v = va_arg(args, ptrdiff_t); break;
            case J: v = va_arg(args, intmax_t); break;
            default: v = va_arg(args, int); break;
            }
            value = v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v;
            if (v < 0) prefix[prefix_size++] = '-';
            else if (plus) prefix[prefix_size++] = '+';
            else if (space) prefix[prefix_size++] = ' ';
        } else {
            switch (length) {
            case HH: value = (unsigned char)va_arg(args, unsigned int); break;
            case H: value = (unsigned short)va_arg(args, unsigned int); break;
            case L: value = va_arg(args, unsigned long); break;
            case LL: value = va_arg(args, unsigned long long); break;
            case Z: case T: value = va_arg(args, size_t); break;
            case J: value = va_arg(args, uintmax_t); break;
            default: value = va_arg(args, unsigned int); break;
            }
        }
        unsigned base = spec == 'o' ? 8 : (spec == 'x' || spec == 'X' || spec == 'p') ? 16 : 10;
        if ((alternate && value && base == 16) || spec == 'p') {
            prefix[0] = '0'; prefix[1] = spec == 'X' ? 'X' : 'x'; prefix_size = 2;
        }
        char digits[64]; size_t n = 0;
        const char *alphabet = spec == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
        if (value || precision != 0 || spec == 'p') {
            do { digits[sizeof(digits) - 1 - n++] = alphabet[value % base]; value /= base; } while (value);
        }
        size_t zeros = precision > 0 && (size_t)precision > n ? (size_t)precision - n : 0;
        if (alternate && base == 8 && !zeros && (!n || digits[sizeof(digits) - n] != '0')) zeros = 1;
        size_t total = prefix_size + zeros + n;
        size_t spaces = (size_t)width > total ? (size_t)width - total : 0;
        if (zero && !left && precision < 0) { zeros += spaces; spaces = 0; }
        if (!left) pad(out, ' ', spaces);
        emit(out, prefix, prefix_size);
        pad(out, '0', zeros);
        emit(out, digits + sizeof(digits) - n, n);
        if (left) pad(out, ' ', spaces);
    }
    goto done;
overflow:
    errno = EOVERFLOW; out->error = 1; goto done;
unsupported:
    errno = EINVAL; out->error = 1;
done:
    va_end(args);
    if (!out->file && out->capacity) {
        size_t index = out->count < out->capacity ? out->count : out->capacity - 1;
        out->buffer[index] = 0;
    }
    return out->error ? EOF : (int)out->count;
}

int vsnprintf(char *buffer, size_t size, const char *fmt, va_list args) {
    if ((!buffer && size) || !fmt) { errno = EINVAL; return EOF; }
    output_t out = { .buffer = buffer, .capacity = size };
    return format(&out, fmt, args);
}
int snprintf(char *buffer, size_t size, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int result = vsnprintf(buffer, size, fmt, args); va_end(args); return result;
}
int vsprintf(char *buffer, const char *fmt, va_list args) { return vsnprintf(buffer, SIZE_MAX, fmt, args); }
int sprintf(char *buffer, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int result = vsprintf(buffer, fmt, args); va_end(args); return result;
}
int vfprintf(FILE *file, const char *fmt, va_list args) {
    if (!fmt || fileno(file) < 0) return EOF;
    output_t out = { .file = file };
    return format(&out, fmt, args);
}
int fprintf(FILE *file, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int result = vfprintf(file, fmt, args); va_end(args); return result;
}
int vprintf(const char *fmt, va_list args) { return vfprintf(stdout, fmt, args); }
int printf(const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int result = vprintf(fmt, args); va_end(args); return result;
}
