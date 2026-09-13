#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL stdiocheck line %d errno=%d\n", __LINE__, errno); return 1; } } while (0)
static int formatted(char *buf, size_t size, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int n = vsnprintf(buf, size, fmt, args); va_end(args); return n;
}
int main(void) {
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
