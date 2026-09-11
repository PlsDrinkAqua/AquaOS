#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    for (int i = 0; i < 5; i++) {
        printf("yieldtest step %d\n", i);
        yield();
    }

    exit(0);
}
