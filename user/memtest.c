#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zenos/task.h>

int main(void) {
    char *a;
    char *b;
    char *c;

    puts("memtest: before allocations");
    zenos_taskinfo();

    a = malloc(32);
    b = malloc(64);
    if (!a || !b) {
        puts("memtest: malloc failed");
        exit(1);
    }

    for (int i = 0; i < 31; i++) {
        a[i] = 'A' + (i % 26);
    }
    a[31] = '\0';

    for (int i = 0; i < 63; i++) {
        b[i] = 'a' + (i % 26);
    }
    b[63] = '\0';

    printf("a=%x %s\n", (unsigned)a, a);
    printf("b=%x %s\n", (unsigned)b, b);

    free(a);
    c = malloc(16);
    if (!c) {
        puts("memtest: second malloc failed");
        exit(1);
    }

    for (int i = 0; i < 15; i++) {
        c[i] = '0' + (i % 10);
    }
    c[15] = '\0';

    printf("c=%x %s\n", (unsigned)c, c);
    puts("memtest: after allocations");
    zenos_taskinfo();

    exit(0);
}
