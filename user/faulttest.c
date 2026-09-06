#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void _start(void) {
    volatile unsigned int *invalid = (volatile unsigned int *)0;

    if (write(1, (const void *)invalid, 4) != -1) {
        puts("faulttest: ERROR - invalid syscall buffer was accepted");
        exit(1);
    }
    puts("faulttest: invalid syscall buffer rejected");

    puts("faulttest: writing to an unmapped address");
    *invalid = 0xDEADBEEF;

    puts("faulttest: ERROR - write unexpectedly succeeded");
    exit(1);
}
