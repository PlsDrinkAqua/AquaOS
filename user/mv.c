#include "file_ops.h"

static int move(const char *old, const char *new) {
    char source[256], target[256];
    if (file_paths(old, new, source, target) < 0) goto failed;
    if (rename(source, target) == 0) return 0;
    if (errno != EXDEV && errno != ENOSPC) goto failed;
    if (copy_file(source, target) < 0) goto failed;
    if (unlink(source) == 0) return 0;
    fprintf(stderr, "mv: copied destination but could not remove %s (errno=%d)\n", source, errno);
    return 1;
failed:
    fprintf(stderr, "mv: cannot move %s to %s (errno=%d)\n", old, new, errno);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) { puts("usage: mv <source> [source ...] <destination>"); return 1; }
    if (argc > 3 && directory(argv[argc - 1]) != 1) {
        fputs("mv: multiple sources require a destination directory\n", stderr); return 1;
    }
    int status = 0;
    for (int i = 1; i < argc - 1; i++) status |= move(argv[i], argv[argc - 1]);
    return status;
}
