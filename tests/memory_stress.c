#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(expr) do { if (!(expr)) { \
    printf("FAIL memcheck line %d errno=%d\n", __LINE__, errno); exit(1); \
} } while (0)

static void heap_checks(void) {
    void *initial = sbrk(0);
    CHECK(malloc(SIZE_MAX) == NULL);
    CHECK(malloc(SIZE_MAX - 7) == NULL);
    CHECK(calloc(SIZE_MAX, 2) == NULL);
    CHECK(sbrk(INTPTR_MIN) == (void *)-1);
    CHECK(sbrk(INTPTR_MAX) == (void *)-1);
    CHECK(sbrk(0) == initial);
    for (int pass = 0; pass < 3; pass++) {
        unsigned char *p = sbrk(8192);
        CHECK(p == initial);
        for (int i = 0; i < 8192; i++) CHECK(p[i] == 0);
        for (int i = 0; i < 8192; i++) p[i] = 0xab;
        CHECK(sbrk(-8192) == p + 8192);
    }
    /* Force partial mapping followed by physical-memory exhaustion. */
    for (int pass = 0; pass < 3; pass++) {
        CHECK(sbrk(128 * 1024 * 1024) == (void *)-1);
        CHECK(sbrk(0) == initial);
        unsigned char *p = sbrk(8192);
        CHECK(p == initial);
        for (int i = 0; i < 8192; i++) CHECK(p[i] == 0);
        CHECK(sbrk(-8192) == p + 8192);
    }
    void *parts[64];
    for (int pass = 0; pass < 20; pass++) {
        for (int i = 0; i < 64; i++) {
            unsigned char *p = parts[i] = malloc(17 + i * 3);
            CHECK(p && ((uintptr_t)p & 7) == 0);
            for (int j = 0; j < 17 + i * 3; j++) p[j] = i;
        }
        for (int i = 0; i < 64; i++) {
            unsigned char *p = parts[i];
            for (int j = 0; j < 17 + i * 3; j++) CHECK(p[j] == i);
        }
        for (int i = 0; i < 64; i += 2) free(parts[i]);
        for (int i = 1; i < 64; i += 2) free(parts[i]);
    }
    /* Direct sbrk users own the gap; free must not merge over it. */
    unsigned char *gap = sbrk(4097);
    CHECK(gap != (void *)-1);
    for (int i = 0; i < 4097; i++) gap[i] = 0x5a;
    void *big = malloc(32768); CHECK(big && ((uintptr_t)big & 7) == 0); free(big);
    big = malloc(49152); CHECK(big);
    for (int i = 0; i < 49152; i++) ((unsigned char *)big)[i] = 0xa5;
    for (int i = 0; i < 4097; i++) CHECK(gap[i] == 0x5a);
    free(big);
}

int main(void) {
    heap_checks();
    for (int i = 0; i < 40; i++) {
        int pid = fork(); CHECK(pid >= 0);
        if (!pid) {
            /* A user protection fault must kill only this child. */
            if (i % 10 == 0) {
                #ifdef __x86_64__
                *(volatile unsigned *)UINT64_C(0xffffffff80000000) = 1;
#else
                *(volatile unsigned *)0xc0000000U = 1;
#endif
                exit(1);
            }
            unsigned char *p = malloc(16384); CHECK(p);
            p[0] = 1; p[16383] = 2;
            exit(0);
        }
        CHECK(wait(pid) == (i % 10 == 0 ? 142 : 0));
        /* Descriptor exhaustion must not poison future pipe allocations. */
        int fds[16], count = 0;
        for (;;) {
            int pair[2];
            if (pipe(pair) < 0) { CHECK(errno == EMFILE); break; }
            CHECK(count + 2 <= 16);
            fds[count++] = pair[0]; fds[count++] = pair[1];
        }
        for (int j = 0; j < count; j++) CHECK(close(fds[j]) == 0);
    }
    puts("memory reliability checks passed");
    return 0;
}
