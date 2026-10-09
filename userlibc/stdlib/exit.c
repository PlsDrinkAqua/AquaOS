#include <stdlib.h>
#include <zenos/syscall.h>

void exit(int status) {
    zenos_syscall1(SYS_EXIT, status);

    for (;;) {
    }
}

void abort(void) { exit(134); }

#include <stdio.h>
void zenos_assert_fail(const char *expression, const char *file, int line) {
    fprintf(stderr, "Assertion failed: %s (%s:%d)\n", expression, file, line);
    abort();
}
