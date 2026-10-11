#include "file_ops.h"

static int copy(const char *old, const char *new) {
    char source[256], target[256];
    if (file_paths(old, new, source, target) < 0) goto failed;
    if (!strcmp(source, target)) {
        fprintf(stderr, "cp: %s and %s are the same path\n", old, new); return 1;
    }
    if (copy_file(source, target) == 0) return 0;
failed:
    fprintf(stderr, "cp: cannot copy %s to %s (errno=%d)\n", old, new, errno);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) { puts("usage: cp <source> [source ...] <destination>"); return 1; }
    if (argc > 3 && directory(argv[argc - 1]) != 1) {
        fputs("cp: multiple sources require a destination directory\n", stderr); return 1;
    }
    int status = 0;
    for (int i = 1; i < argc - 1; i++) status |= copy(argv[i], argv[argc - 1]);
    return status;
}
