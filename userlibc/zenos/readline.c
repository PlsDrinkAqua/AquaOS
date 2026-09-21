#include <stdio.h>
#include <unistd.h>
#include <zenos/readline.h>
#include <zenos/keys.h>

char *zenos_readline_highlight(char *buf, size_t size,
                              void (*complete)(char *, size_t),
                              void (*render)(const char *)) {
    size_t len = 0;
    size_t displayed = 0;

    if (size == 0) {
        return buf;
    }

    buf[0] = '\0';
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

        if (c == '\t' && complete) {
            buf[len] = '\0';
            complete(buf, size);
            while (buf[len]) {
                if (!render) putchar(buf[len]);
                len++;
            }
            goto redraw;
        }

        if (c == '\n') {
            putchar('\n');
            buf[len] = '\0';
            return buf;
        }

        if (c == '\b') {
            if (len > 0) {
                len--;
                if (!render) putchar('\b');
            }
            goto redraw;
        }

        if (render && (unsigned char)c < 32) continue;
        if (len + 1 < size) {
            buf[len++] = c;
            if (!render) putchar(c);
        }
redraw:
        buf[len] = '\0';
        if (render) {
            while (displayed) { putchar('\b'); displayed--; }
            render(buf);
            displayed = len;
        }
    }
}

char *zenos_readline_complete(char *buf, size_t size,
                             void (*complete)(char *, size_t)) {
    return zenos_readline_highlight(buf, size, complete, NULL);
}

char *zenos_readline(char *buf, size_t size) {
    return zenos_readline_complete(buf, size, NULL);
}
