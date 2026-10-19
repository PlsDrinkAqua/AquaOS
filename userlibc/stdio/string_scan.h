/* Bounded string input for the numeric scanner; EOF never reads past NUL. */
#ifndef AQUAOS_STRING_SCAN_H
#define AQUAOS_STRING_SCAN_H
#include <stddef.h>
typedef struct {
    const unsigned char *text;
    size_t pos, limit;
    int eof, failed;
} string_scan;
static inline int shgetc(string_scan *s) {
    if (s->pos == s->limit || !s->text[s->pos]) { s->eof = 1; return -1; }
    s->eof = 0;
    return s->text[s->pos++];
}
static inline void shunget(string_scan *s) {
    if (s->eof) s->eof = 0;
    else if (s->pos) s->pos--;
}
static inline void shlim(string_scan *s, size_t unused) {
    (void)unused; s->failed = 1;
}
long double __aquaos_floatscan(string_scan *s, int precision, int partial_ok);
#endif
