#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef HOST_TEST
#include <unistd.h>
#endif

#ifdef HOST_TEST
int errno;
#endif
#define CHECK(x) do { if (!(x)) { printf("FAIL libccheck line %d\n", __LINE__); return 1; } } while (0)
static int compare(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
static int values[1024];
int main(void) {
#ifndef HOST_TEST
    if (getenv("AQUAOS_ENV_CHILD")) {
        CHECK(getenv("AQUAOS_ENV_VALUE") && !strcmp(getenv("AQUAOS_ENV_VALUE"), "inherited value"));
        CHECK(setenv("AQUAOS_ENV_VALUE", "child value", 1) == 0);
        CHECK(unsetenv("AQUAOS_ENV_CHILD") == 0);
        return 0;
    }
    CHECK(setenv("AQUAOS_ENV_VALUE", "inherited value", 1) == 0);
    CHECK(setenv("AQUAOS_ENV_VALUE", "ignored", 0) == 0);
    CHECK(!strcmp(getenv("AQUAOS_ENV_VALUE"), "inherited value"));
    CHECK(setenv("AQUAOS_ENV_CHILD", "1", 1) == 0);
    int child = fork(); CHECK(child >= 0);
    if (!child) {
        const char *args[] = {"/bin/libccheck", NULL};
        exec("/bin/libccheck", args);
        exit(99);
    }
    CHECK(wait(child) == 0);
    CHECK(!strcmp(getenv("AQUAOS_ENV_VALUE"), "inherited value"));
    child = spawn("/bin/libccheck"); CHECK(child > 0);
    CHECK(wait(child) == 0);
    CHECK(!strcmp(getenv("AQUAOS_ENV_VALUE"), "inherited value"));
    CHECK(unsetenv("AQUAOS_ENV_CHILD") == 0);
    CHECK(unsetenv("AQUAOS_ENV_VALUE") == 0);
    CHECK(getenv("AQUAOS_ENV_VALUE") == NULL);
    CHECK(setenv("AQUAOS_ENV_EMPTY", "", 1) == 0);
    CHECK(getenv("AQUAOS_ENV_EMPTY") && !*getenv("AQUAOS_ENV_EMPTY"));
    CHECK(unsetenv("AQUAOS_ENV_EMPTY") == 0);
    const char *exec_args[] = {"/bin/libccheck", NULL};
    errno = 0;
    CHECK(execve("/bin/libccheck", exec_args, (const char *const *)(uintptr_t)-1) == -1 && errno == EFAULT);
    char oversized[1025]; memset(oversized, 'x', sizeof(oversized) - 1); oversized[1024] = 0;
    const char *big_env[] = {oversized, NULL};
    errno = 0; CHECK(execve("/bin/libccheck", exec_args, big_env) == -1 && errno == E2BIG);

    errno = 0; CHECK(setenv("bad=name", "x", 1) == -1 && errno == EINVAL);
#endif
    char buf[32];
    CHECK(memset(buf, 0x141, 4) == buf && buf[3] == 'A');
    CHECK(memcpy(buf, "abcdef", 7) == buf);
    CHECK(memmove(buf + 2, buf, 5) == buf + 2);
    CHECK(memcmp(buf, "ababcde", 7) == 0);
    memmove(buf, buf + 2, 5); CHECK(memcmp(buf, "abcde", 5) == 0);
    memmove(buf, buf, 5); CHECK(memcmp(buf, "abcde", 5) == 0);
    CHECK(memcmp("\200", "\177", 1) > 0);
    CHECK(memcmp("x", "y", 0) == 0);
    CHECK(strcpy(buf, "ababa") == buf);
    CHECK(strchr(buf, 'b') == buf + 1 && strrchr(buf, 'b') == buf + 3);
    CHECK(strchr(buf, 0) == buf + 5 && strrchr(buf, 0) == buf + 5);
    CHECK(strchr(buf, 'x') == NULL && strrchr(buf, 'x') == NULL);
    CHECK(strstr(buf, "aba") == buf && strstr(buf, "bab") == buf + 1);
    CHECK(strstr(buf, "") == buf && strstr(buf, "ababab") == NULL);
    char *end;
    const char *text = " \t-0x2a!";
    CHECK(strtol(text, &end, 0) == -42 && *end == '!');
    text = " +junk"; CHECK(strtol(text, &end, 10) == 0 && end == text);
    text = "0x"; CHECK(strtol(text, &end, 0) == 0 && end == text + 1);
    CHECK(strtol("0779", &end, 0) == 63 && *end == '9');
    CHECK(strtol("Zz", &end, 36) == 1295 && !*end);
    CHECK(strtol("101", &end, 2) == 5 && !*end);
    errno = 0; text = "12";
    CHECK(strtol(text, &end, 1) == 0 && errno == EINVAL && end == text);
    errno = EIO; CHECK(strtol("42", NULL, 10) == 42 && errno == EIO);
    errno = 0;
    CHECK(strtoll("9223372036854775807", &end, 10) == LLONG_MAX && !errno && !*end);
    CHECK(strtoll("-9223372036854775808", &end, 10) == LLONG_MIN && !errno && !*end);
    CHECK(strtoll("9223372036854775808000!", &end, 10) == LLONG_MAX && errno == ERANGE && *end == '!');
    errno = 0;
    CHECK(strtoll("-9223372036854775809", NULL, 10) == LLONG_MIN && errno == ERANGE);
    errno = 0;
    CHECK(strtoull("18446744073709551615", &end, 10) == ULLONG_MAX && !errno && !*end);
    CHECK(strtoull("-1", NULL, 10) == ULLONG_MAX && !errno);
    CHECK(strtoull("18446744073709551616!", &end, 10) == ULLONG_MAX && errno == ERANGE && *end == '!');
#if LONG_MAX == 2147483647L
    errno = 0;
    CHECK(strtol("-2147483648", NULL, 10) == LONG_MIN && !errno);
    CHECK(strtol("2147483648", NULL, 10) == LONG_MAX && errno == ERANGE);
    errno = 0;
    CHECK(strtoul("4294967295", NULL, 10) == ULONG_MAX && !errno);
    CHECK(strtoul("4294967296", NULL, 10) == ULONG_MAX && errno == ERANGE);
#endif
    CHECK(strtoul("-1", NULL, 10) == ULONG_MAX);
    CHECK(atoi(" -123rest") == -123);
    for (int pattern = 0; pattern < 4; pattern++) {
        for (int i = 0; i < 1024; i++)
            values[i] = pattern == 0 ? 1023 - i : pattern == 1 ? i : pattern == 2 ? 7 : (i * 7919) % 97;
        long sum = 0; for (int i = 0; i < 1024; i++) sum += values[i];
        qsort(values, 1024, sizeof(values[0]), compare);
        for (int i = 1; i < 1024; i++) CHECK(values[i - 1] <= values[i]);
        for (int i = 0; i < 1024; i++) sum -= values[i];
        CHECK(sum == 0);
    }
    qsort(values, 0, sizeof(int), compare);
    qsort(values, 1, sizeof(int), compare);
#ifndef HOST_TEST
    unsigned char *p = realloc(NULL, 32); CHECK(p);
    for (unsigned i = 0; i < 32; i++) p[i] = i;
    void *guard = malloc(128); CHECK(guard);
    unsigned char *grown = realloc(p, 16384); CHECK(grown);
    for (unsigned i = 0; i < 32; i++) CHECK(grown[i] == i);
    p = realloc(grown, 16); CHECK(p == grown);
    errno = 0; CHECK(realloc(p, SIZE_MAX) == NULL && errno == ENOMEM);
    for (unsigned i = 0; i < 16; i++) CHECK(p[i] == i);
    CHECK(realloc(p, 0) == NULL);
    free(guard);
#endif
    puts("libc core checks passed");
    return 0;
}
