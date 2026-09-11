#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int copy(int fd) {
    char buf[512];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) { puts("cat: read failed"); return 1; }
        if (!n) return 0;
        ssize_t done = 0;
        while (done < n) {
            ssize_t written = write(1, buf + done, (size_t)(n - done));
            if (written <= 0) return 1;
            done += written;
        }
    }
}

int main(int argc, char **argv) {
    int status = 0;
    if (argc < 2) return copy(0);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) { status |= copy(0); continue; }
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: cannot open %s\n", argv[i]);
            status = 1;
            continue;
        }
        status |= copy(fd);
        close(fd);
    }
    return status;
}
