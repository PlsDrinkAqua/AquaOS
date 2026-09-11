#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int is_directory(const char *path, const char *name) {
    char child[512], entry[256];
    size_t length = strlen(path), name_length = strlen(name);
    if (length + name_length + 2 > sizeof(child)) return 0;
    for (size_t i = 0; i < length; i++) child[i] = path[i];
    if (length && child[length - 1] != '/') child[length++] = '/';
    for (size_t i = 0; i <= name_length; i++) child[length + i] = name[i];

    int fd = open(child, O_RDONLY);
    if (fd < 0) return 0;
    /* readdir succeeds for directories (including empty ones), and rejects files. */
    int result = readdir(fd, entry, sizeof(entry));
    close(fd);
    return result >= 0;
}

static int list(const char *path) {
    char name[256];
    int fd = open(path, O_RDONLY);
    if (fd < 0) { printf("ls: cannot open %s\n", path); return 1; }
    int result;
    while ((result = readdir(fd, name, sizeof(name))) > 0)
        printf("%s%s\n", name, is_directory(path, name) ? "/" : "");
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
