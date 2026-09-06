#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ITERATIONS 24

void _start(void) {
    for (int i = 0; i < ITERATIONS; i++) {
        if (spawn("/does-not-exist") != -1) {
            puts("tasktest: failed spawn unexpectedly succeeded");
            exit(1);
        }
    }

    for (int i = 0; i < ITERATIONS; i++) {
        int pid = spawn("/hello");
        if (pid < 0) {
            printf("tasktest: spawn failed at iteration %d\n", i);
            exit(2);
        }
        if (wait(pid) != 0) {
            printf("tasktest: wait failed at iteration %d\n", i);
            exit(3);
        }
    }

    if (wait(0x7fffffff) != -1) {
        puts("tasktest: invalid wait unexpectedly succeeded");
        exit(4);
    }

    puts("tasktest: PASS");
    exit(0);
}
