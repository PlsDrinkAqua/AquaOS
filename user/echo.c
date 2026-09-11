#include <string.h>
#include <unistd.h>

static int output(const char *text) {
    size_t left = strlen(text);
    while (left) {
        ssize_t n = write(1, text, left);
        if (n <= 0) return 1;
        text += n;
        left -= (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1 && output(" ")) return 1;
        if (output(argv[i])) return 1;
    }
    return output("\n");
}
