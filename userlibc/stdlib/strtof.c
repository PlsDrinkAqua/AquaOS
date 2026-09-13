#include <stdlib.h>
#include <stdint.h>
#include "../stdio/string_scan.h"
static long double convert(const char *text, char **end, int precision) {
    string_scan input = { (const unsigned char *)text, 0, SIZE_MAX, 0, 0 };
    long double value = __zenos_floatscan(&input, precision, 1);
    if (end) *end = (char *)text + (input.failed ? 0 : input.pos);
    return value;
}
float strtof(const char *text, char **end) { return convert(text, end, 0); }
double strtod(const char *text, char **end) { return convert(text, end, 1); }
long double strtold(const char *text, char **end) { return convert(text, end, 2); }
