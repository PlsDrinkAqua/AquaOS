#include <stdlib.h>
#include <aquaos/syscall.h>

void exit(int status) {
    aquaos_syscall1(SYS_EXIT, status);

    for (;;) {
    }
}

void abort(void) { exit(134); }

#include <stdio.h>
void aquaos_assert_fail(const char *expression, const char *file, int line) {
    fprintf(stderr, "Assertion failed: %s (%s:%d)\n", expression, file, line);
    abort();
}
