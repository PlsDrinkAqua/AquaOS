#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zenos/readline.h>
#include <zenos/keys.h>

#define HISTORY_COUNT 32
#define HISTORY_SIZE 128
static char history[HISTORY_COUNT][HISTORY_SIZE];
static size_t readline_history_count;

static void move_cursor(size_t count, int forward) {
    if (count) {
        char sequence[32];
        snprintf(sequence, sizeof(sequence), "\033[%u;%u~", forward ? 2 : 1,
                 (unsigned)count);
        fputs(sequence, stdout);
    }
}

static void remember(const char *line, size_t len) {
    if (!len || len >= HISTORY_SIZE) return;
    if (readline_history_count && !strcmp(history[readline_history_count - 1], line)) return;
    if (readline_history_count == HISTORY_COUNT) {
        memmove(history, history + 1, (HISTORY_COUNT - 1) * HISTORY_SIZE);
        readline_history_count--;
    }
    strcpy(history[readline_history_count++], line);
}

char *zenos_readline_highlight(char *buf, size_t size,
                              void (*complete)(char *, size_t),
                              void (*render)(const char *)) {
    size_t len = 0, cursor = 0, shown = 0, screen_cursor = 0;
    size_t entry = readline_history_count, draft_cursor = 0;
    if (!size) return buf;
    char draft[size];
    draft[0] = buf[0] = '\0';
    for (;;) {
        unsigned char c = 0;
        ssize_t n = read(0, &c, 1);
        if (n <= 0 || c == '\n' || c == 3) {
            move_cursor(len - cursor, 1);
            if (c == 3 && n > 0) {
                puts("^C"); buf[0] = '\0'; return buf;
            }
            if (n > 0) putchar('\n');
            if (n >= 0) remember(buf, len);
            return n < 0 || (n == 0 && !len) ? NULL : buf;
        }
        if (c == ZENOS_KEY_LEFT) { if (cursor) cursor--; }
        else if (c == ZENOS_KEY_RIGHT) { if (cursor < len) cursor++; }
        else if (c == ZENOS_KEY_HOME) cursor = 0;
        else if (c == ZENOS_KEY_END) cursor = len;
        else if (c == ZENOS_KEY_UP || c == ZENOS_KEY_DOWN) {
            if (c == ZENOS_KEY_UP && entry) {
                if (entry == readline_history_count) { strcpy(draft, buf); draft_cursor = cursor; }
                entry--;
            } else if (c == ZENOS_KEY_DOWN && entry < readline_history_count) entry++;
            else continue;
            const char *source = entry == readline_history_count ? draft : history[entry];
            len = strlen(source);
            if (len >= size) len = size - 1;
            memcpy(buf, source, len); buf[len] = '\0';
            cursor = entry == readline_history_count ? draft_cursor : len;
            if (cursor > len) cursor = len;
        } else if (c == '\b') {
            if (cursor) {
                memmove(buf + cursor - 1, buf + cursor, len - cursor + 1);
                cursor--; len--;
            }
        } else if (c == ZENOS_KEY_DELETE) {
            if (cursor < len) { memmove(buf + cursor, buf + cursor + 1, len - cursor); len--; }
        } else if (c == '\t' && complete) {
            /* Complete the prefix, preserving the suffix after the cursor. */
            char prefix[size];
            memcpy(prefix, buf, cursor); prefix[cursor] = '\0';
            complete(prefix, size - (len - cursor));
            size_t added = strlen(prefix);
            memmove(buf + added, buf + cursor, len - cursor + 1);
            memcpy(buf, prefix, added);
            len = added + len - cursor; cursor = added;
        } else if (c >= 32 && c < 127 && len + 1 < size) {
            memmove(buf + cursor + 1, buf + cursor, len - cursor + 1);
            buf[cursor++] = c; len++;
        } else continue;

        if (c == ZENOS_KEY_LEFT || c == ZENOS_KEY_RIGHT ||
            c == ZENOS_KEY_HOME || c == ZENOS_KEY_END) {
            if (cursor >= screen_cursor) move_cursor(cursor - screen_cursor, 1);
            else move_cursor(screen_cursor - cursor, 0);
            screen_cursor = cursor;
            continue;
        }

        /* Keep intermediate erasing, highlighting and cursor moves off screen. */
        fputs("\033[3~", stdout);
        /* Erase from the old end so shortened and wrapped lines leave no tail. */
        move_cursor(shown - screen_cursor, 1);
        for (size_t i = 0; i < shown; i++) putchar('\b');
        if (render) render(buf);
        else fputs(buf, stdout);
        move_cursor(len - cursor, 0);
        fputs("\033[4~", stdout);
        shown = len; screen_cursor = cursor;
    }
}

char *zenos_readline_complete(char *buf, size_t size,
                             void (*complete)(char *, size_t)) {
    return zenos_readline_highlight(buf, size, complete, NULL);
}

char *zenos_readline(char *buf, size_t size) {
    return zenos_readline_complete(buf, size, NULL);
}
