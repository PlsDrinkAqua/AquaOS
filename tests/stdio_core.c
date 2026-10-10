#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL stdiocheck line %d errno=%d\n", __LINE__, errno); return 1; } } while (0)
static int formatted(char *buf, size_t size, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int n = vsnprintf(buf, size, fmt, args); va_end(args); return n;
}
static int scanned(const char *text, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int n = vsscanf(text, fmt, args); va_end(args); return n;
}
static int numeric_checks(void) {
    char buf[128], word[12], pair[2];
    int a, b, n = -1; unsigned x;
    CHECK(scanned(" -42 077 0xff tail", "%d %i %x %4s%n", &a, &b, &x, word, &n) == 4);
    CHECK(a == -42 && b == 63 && x == 255 && !strcmp(word, "tail") && n == 18);
    CHECK(sscanf("abc-123!", "%3[a-z]%*c%u%n", word, &x, &n) == 2);
    CHECK(!strcmp(word, "abc") && x == 123 && n == 7);
    CHECK(sscanf("]-ok?", "%4[]a-z-]%c", word, pair) == 2);
    CHECK(!strcmp(word, "]-ok") && pair[0] == '?');
    CHECK(sscanf("xy", "%2c%n", pair, &n) == 1 && !memcmp(pair, "xy", 2) && n == 2);
    CHECK(sscanf("", "%d", &a) == EOF && sscanf("x", "%d", &a) == 0);
    CHECK(sscanf("12", "%*d%d", &a) == EOF);
    signed char tiny; unsigned short shorty; long long big; size_t size; ptrdiff_t diff;
    CHECK(sscanf("-128 65535 -9223372036854775808 17 -3", "%hhd %hu %lld %zu %td", &tiny, &shorty, &big, &size, &diff) == 5);
    CHECK(tiny == -128 && shorty == 65535 && big == (-9223372036854775807LL-1) && size == 17 && diff == -3);
    float f; double d; long double ld;
    CHECK(sscanf("1.25 -0x1.8p+2 2.5e-3", "%f %lf %Lf%n", &f, &d, &ld, &n) == 3);
    CHECK(f == 1.25f && d == -6 && ld == 0.0025L && n == 21);
    d = 17;
    CHECK(sscanf("100e+", "%lf", &d) == 0 && d == 17);
    CHECK(sscanf("1.25e7", "%4lf%n", &d, &n) == 1 && d == 1.25 && n == 4);
    CHECK(snprintf(buf, sizeof(buf), "%.2f %.3e %.4g %a", 1.25, -6.0, 0.0025, 1.5) > 0);
    CHECK(!strcmp(buf, "1.25 -6.000e+00 0.0025 0x1.8p+0"));
    char *end;
    CHECK(strtod("  -12.5e2rest", &end) == -1250 && !strcmp(end, "rest"));
    CHECK(strtod("1e+", &end) == 1 && !strcmp(end, "e+"));
    const char *bad = "  +x";
    CHECK(strtod(bad, &end) == 0 && end == bad);
    errno = 0; CHECK(isinf(strtof("1e9999", &end)) && errno == ERANGE && !*end);
    errno = 0; CHECK(strtod("1e-9999", &end) == 0 && errno == ERANGE && !*end);
    CHECK(strtod("4.9406564584124654e-324", NULL) == 0x1p-1074);
    CHECK(strtof("0x1p-149", NULL) == 0x1p-149f);
    ld = strtold("1.234567890123456789", NULL);
    CHECK(ld == 1.234567890123456789L);
    CHECK(snprintf(buf, sizeof(buf), "%.19Lf", ld) == 21);
    CHECK(!strcmp(buf, "1.2345678901234567890"));
    ld = strtold("1e4900", NULL);
    CHECK(isfinite(ld) && ld > 1e4899L);
    CHECK(snprintf(buf, sizeof(buf), "%.3Le", ld) == 11 && !strcmp(buf, "1.000e+4900"));
    return 0;
}

static void controls(unsigned short cw, unsigned mxcsr) {
    __asm__ volatile ("fldcw %0; ldmxcsr %1" :: "m"(cw), "m"(mxcsr) : "memory");
}
static int check_controls(unsigned short cw, unsigned mxcsr) {
    unsigned short actual_cw; unsigned actual_mxcsr;
    __asm__ volatile ("fnstcw %0; stmxcsr %1" : "=m"(actual_cw), "=m"(actual_mxcsr));
    return actual_cw == cw && actual_mxcsr == mxcsr;
}
/* Keep live x87 and SSE data across both timer interrupts and explicit yields.
 * This test, like libc, is compiled without compiler-generated SSE. */
static int live_registers(double value, unsigned seed) {
    unsigned vector[4] = {seed, seed+1, seed+2, seed+3}, restored[4];
    double result;
    __asm__ volatile (
        "fldl %2; movups %3, %%xmm7; mov $20, %%edx;"
        "1: mov $1000000, %%ecx; 2: dec %%ecx; jnz 2b;"
        "mov $11, %%eax; int $0x80; dec %%edx; jnz 1b;"
        "fstpl %0; movups %%xmm7, %1"
        : "=m"(result), "=m"(restored)
        : "m"(value), "m"(vector) : "eax", "ecx", "edx", "st", "cc", "memory"
          , "xmm7"
);
    return result == value && !memcmp(vector, restored, sizeof(vector));
}
static int context_checks(void) {
    controls(0x077f, 0x3f80);
    int pid = fork(); CHECK(pid >= 0);
    CHECK(check_controls(0x077f, 0x3f80));
    if (!pid) {
        controls(0x0b7f, 0x5f80);
        CHECK(live_registers(12.5, 0x12345678));
        CHECK(check_controls(0x0b7f, 0x5f80));
        const char *args[] = {"stdiocheck", "exec-fpu", NULL};
        CHECK(exec("/bin/stdiocheck", args) == 0);
        return 1;
    }
    CHECK(live_registers(-37.25, 0xabcdef01));
    CHECK(wait(pid) == 0 && check_controls(0x077f, 0x3f80));
    controls(0x037f, 0x1f80);
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "exec-fpu")) {
        CHECK(check_controls(0x037f, 0x1f80));
        CHECK(numeric_checks() == 0);
        return 0;
    }
    CHECK(numeric_checks() == 0);
    CHECK(context_checks() == 0);

    char buf[128];
    CHECK(snprintf(buf, sizeof(buf), "%+08d %#x %.4s", -42, 42, "hello") == 18);
    CHECK(!strcmp(buf, "-0000042 0x2a hell"));
    CHECK(formatted(buf, sizeof(buf), "%lld/%llu", (-9223372036854775807LL - 1), ~0ULL) == 41);
    CHECK(!strcmp(buf, "-9223372036854775808/18446744073709551615"));
    CHECK(snprintf(buf, sizeof(buf), "%ld %zu %td", -2147483647L - 1, (size_t)17, (ptrdiff_t)-3) == 17);
    CHECK(!strcmp(buf, "-2147483648 17 -3"));
    CHECK(snprintf(buf, 4, "%s", "abcdef") == 6 && !strcmp(buf, "abc"));
    CHECK(snprintf(NULL, 0, "%s:%d", "line", 12) == 7);
    CHECK(sprintf(buf, "%*.*s:%02d", -6, 3, "abcdef", 7) == 9 && !strcmp(buf, "abc   :07"));
    CHECK(snprintf(buf, sizeof(buf), "%#.0o/%.0u/%%", 0, 0) == 4 && !strcmp(buf, "0//%"));
    FILE *f = fopen("/stdio.txt", "w+"); CHECK(f);
    CHECK(fprintf(f, "line %02d: %s\n", 7, "hello") == 15);
    CHECK(ftell(f) == 15 && fflush(f) == 0 && fflush(NULL) == 0);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    CHECK(fgetc(f) == 'l' && ungetc('X', f) == 'X' && ftell(f) == 0);
    CHECK(fseek(f, 0, SEEK_CUR) == 0 && fgetc(f) == 'l');
    rewind(f);
    CHECK(fgets(buf, sizeof(buf), f) == buf && !strcmp(buf, "line 07: hello\n"));
    CHECK(fgetc(f) == EOF && feof(f) && !ferror(f));
    CHECK(ungetc('!', f) == '!' && !feof(f));
    CHECK(fgetc(f) == '!' && fgetc(f) == EOF);
    CHECK(fseek(f, 0, SEEK_SET) == 0 && !feof(f));
    CHECK(fread(buf, 4, 4, f) == 3 && feof(f));
    clearerr(f); CHECK(!feof(f)); CHECK(fclose(f) == 0);
    int fd = open("/stdio.txt", O_RDWR, 0644); CHECK(fd >= 0);
    f = fdopen(fd, "w"); CHECK(f);
    CHECK(fclose(f) == 0); /* fdopen w must not truncate. */
    fd = open("/stdio.txt", O_RDWR); CHECK(fd >= 0);
    f = fdopen(fd, "a+"); CHECK(f);
    int alias = dup(fd); CHECK(alias >= 0 && (fcntl(alias, F_GETFL) & O_APPEND));
    CHECK(fseek(f, 0, SEEK_SET) == 0 && fputs("tail", f) >= 0);
    CHECK(close(alias) == 0 && fclose(f) == 0);
    f = fopen("/stdio.txt", "rb"); CHECK(f);
    CHECK(fread(buf, 1, sizeof(buf), f) == 19);
    buf[19] = 0; CHECK(!strcmp(buf, "line 07: hello\ntail"));
    CHECK(fputc('x', f) == EOF && ferror(f));
    clearerr(f); CHECK(!ferror(f) && !feof(f));
    CHECK(fdopen(fileno(f), "w") == NULL && errno == EINVAL);
    CHECK(fclose(f) == 0);
    CHECK(fopen("/stdio.txt", "r++") == NULL && errno == EINVAL);
    CHECK(fdopen(-1, "r") == NULL && errno == EBADF);
    int pair[2]; CHECK(pipe(pair) == 0);
    FILE *reader = fdopen(pair[0], "r"), *writer = fdopen(pair[1], "w"); CHECK(reader && writer);
    CHECK(fprintf(writer, "pipe:%d", 42) == 7 && fclose(writer) == 0);
    CHECK(fread(buf, 1, sizeof(buf), reader) == 7 && feof(reader));
    CHECK(!memcmp(buf, "pipe:42", 7));
    CHECK(fseek(reader, 0, SEEK_SET) == -1 && errno == ESPIPE);
    CHECK(fclose(reader) == 0);
    CHECK(pipe(pair) == 0 && close(pair[0]) == 0);
    writer = fdopen(pair[1], "w"); CHECK(writer);
    CHECK(fprintf(writer, "broken") == EOF && ferror(writer) && errno == EPIPE);
    CHECK(fclose(writer) == 0);
    FILE *opened[FOPEN_MAX]; unsigned n = 0;
    while (n < FOPEN_MAX && (opened[n] = fopen("/stdio.txt", "r"))) n++;
    CHECK(n < FOPEN_MAX && errno == EMFILE);
    for (unsigned i = 0; i < n; i++) CHECK(fclose(opened[i]) == 0);
    CHECK(remove("/stdio.txt") == 0);
    CHECK(mkdir("/stdio-dir") == 0 && remove("/stdio-dir") == 0);
    puts("stdio checks passed");
    return 0;
}
