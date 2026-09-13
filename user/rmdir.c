#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) { puts("usage: rmdir <path> [path ...]"); return 1; }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            printf("rmdir: cannot remove %s (errno=%d)\n", argv[i], errno);
            status = 1;
        }
    }
    return status;
}
