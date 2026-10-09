#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <libk/stdio.h>
#include <libk/string.h>
#include <stdint.h>

static bool print(const char* data, size_t length) {
    const unsigned char* bytes = (const unsigned char*) data;
    for (size_t i = 0; i < length; i++)
        if (kputchar(bytes[i]) == KEOF)
            return false;
    return true;
}

// Convert unsigned integer x to string in given base (2..16), returns pointer to start
static char* utoa(unsigned long x, unsigned base, char* buf_end) {
    static const char digits[] = "0123456789abcdef";
    char* p = buf_end;
    *--p = '\0';
    do {
        *--p = digits[x % base];
        x /= base;
    } while (x);
    return p;
}

int kprintf(const char* restrict format, ...) {
    va_list parameters;
    va_start(parameters, format);

    int written = 0;

    while (*format != '\0') {
        size_t maxrem = INT_MAX - written;

        if (format[0] != '%' || format[1] == '%') {
            if (format[0] == '%')
                format++;
            size_t amount = 1;
            while (format[amount] && format[amount] != '%')
                amount++;
            if (maxrem < amount) {
                return -1;
            }
            if (!print(format, amount))
                return -1;
            format += amount;
            written += amount;
            continue;
        }

        const char* format_begun_at = format++;
        bool wide = false;
        if (*format == 'l') { wide = true; format++; }

        if (*format == 'c') {
            format++;
            char c = (char) va_arg(parameters, int);
            if (!maxrem)
                return -1;
            if (!print(&c, 1))
                return -1;
            written++;
        } else if (*format == 's') {
            format++;
            const char* str = va_arg(parameters, const char*);
            size_t len = kstrlen(str);
            if (maxrem < len)
                return -1;
            if (!print(str, len))
                return -1;
            written += len;
        } else if (*format == 'd' || *format == 'u' || *format == 'x' || *format == 'p') {
            char numbuf[32];
            char* s;
            unsigned long val;
            bool negative = false;

            if (*format == 'd') {
                long v = wide ? va_arg(parameters, long) : va_arg(parameters, int);
                if (v < 0) {
                    negative = true;
                    val = 0UL - (unsigned long)v;
                } else {
                    val = (unsigned long)v;
                }
                s = utoa(val, 10, numbuf + sizeof(numbuf));
            } else {
                val = *format == 'p' ? (uintptr_t)va_arg(parameters, void *) :
                      wide ? va_arg(parameters, unsigned long) : va_arg(parameters, unsigned int);
                s = utoa(val, (*format == 'x' || *format == 'p') ? 16 : 10, numbuf + sizeof(numbuf));
            }
            size_t len = kstrlen(s) + (negative ? 1 : 0);
            if (maxrem < len) {
                return -1;
            }
            if (negative) {
                if (!print("-", 1))
                    return -1;
                written++;
            }
            if (!print(s, kstrlen(s)))
                return -1;
            written += len;
            format++;
        } else {
            // Unknown specifier: print it literally
            format = format_begun_at;
            size_t len = kstrlen(format);
            if (maxrem < len)
                return -1;
            if (!print(format, len))
                return -1;
            written += len;
            format += len;
        }
    }

    va_end(parameters);
    return written;
}
