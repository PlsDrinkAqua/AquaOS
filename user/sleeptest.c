#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void _start(void) {
    puts("sleeptest: before sleep");
    sleep(100);
    puts("sleeptest: after 100 ticks");
    exit(0);
}
