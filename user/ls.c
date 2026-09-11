#include <stdio.h>
#include <unistd.h>

static int list(const char *path) {
    char name[256];
    int fd = open(path, O_RDONLY);
    if (fd < 0) { printf("ls: cannot open %s\n", path); return 1; }
    int result;
    while ((result = readdir(fd, name, sizeof(name))) > 0) puts(name);
    close(fd);
    if (result < 0) { printf("ls: cannot read %s\n", path); return 1; }
    return 0;
}

int main(int argc, char **argv) {
    int status = 0;
    if (argc < 2) return list(".");
    for (int i = 1; i < argc; i++) status |= list(argv[i]);
    return status;
}
