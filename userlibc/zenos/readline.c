#include <stdio.h>
#include <unistd.h>
#include <zenos/readline.h>
#include <zenos/keys.h>

char *zenos_readline(char *buf, size_t size) {
    size_t len = 0;

    if (size == 0) {
        return buf;
    }

    for (;;) {
        char c;

        ssize_t n = read(0, &c, 1);
        if (n <= 0) {
            buf[len] = '\0';
            return n == 0 && len > 0 ? buf : NULL;
        }

        if ((unsigned char)c >= ZENOS_KEY_UP && (unsigned char)c <= ZENOS_KEY_RIGHT)
            continue;

        if (c == 3) {
            puts("^C");
            buf[0] = '\0';
            return buf;
        }

        if (c == '\n') {
            putchar('\n');
            buf[len] = '\0';
            return buf;
        }

        if (c == '\b') {
            if (len > 0) {
                len--;
                putchar('\b');
            }
            continue;
        }

        if (len + 1 < size) {
            buf[len++] = c;
            putchar(c);
        }
    }
}
