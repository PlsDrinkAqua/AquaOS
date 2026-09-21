#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include "../kernel/arch/i386/tty.c"

void *kmemcpy(void *restrict d, const void *restrict s, size_t n) { return memcpy(d, s, n); }
size_t kstrlen(const char *s) { return strlen(s); }
static const char *input;
static int mock_open(const char *path, int flags, ...) {
    (void)flags;
    return !strcmp(path, "/bin/cat") || !strcmp(path, "./app") ? 9 : -1;
}
static int mock_close(int fd) { (void)fd; return 0; }
static ssize_t mock_read(int fd, void *buf, size_t n) {
    if (fd == 9) { assert(n == 4); memcpy(buf, "\177ELF", 4); return 4; }
    if (!*input) return 0;
    *(char *)buf = *input++;
    return 1;
}
static int mock_putchar(int c) { terminal_putchar(c); return c; }
static int mock_puts(const char *s) { terminal_writestring(s); terminal_putchar('\n'); return 0; }
static int mock_fputs(const char *s, FILE *f) { (void)f; terminal_writestring(s); return 0; }
static size_t mock_fwrite(const void *p, size_t s, size_t n, FILE *f) {
    (void)f; terminal_write(p, s * n); return n;
}
#define open mock_open
#define close mock_close
#define read mock_read
#define putchar mock_putchar
#define puts mock_puts
#define fputs mock_fputs
#define fwrite mock_fwrite
#include "../userlibc/zenos/readline.c"
#define LINE_MAX 128
static int completion_separator(char c) {
    return c == ' ' || c == '\t' || c == '|' || c == '<' || c == '>' || c == '&';
}
#include "../user/shell_highlight.h"
static void complete(char *buf, size_t size) {
    assert(size >= 4);
    if (!strcmp(buf, "ca")) strcpy(buf, "cat");
}
static uint16_t screen[80 * 25];
static void expect(size_t index, char c, unsigned color) {
    assert(screen[index] == vga_entry(c, color));
}
int main(void) {
    char buf[LINE_MAX];
    terminal_buffer = screen;
    terminal_clear();
    input = "ca\t file | cd && nope # comment\n";
    assert(zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line) == buf);
    assert(!strcmp(buf, "cat file | cd && nope # comment"));
    expect(0, 'c', 13); expect(4, 'f', 7); expect(9, '|', 14);
    expect(11, 'c', 11); expect(14, '&', 14); expect(17, 'n', 12);
    expect(22, '#', 8); assert(terminal_color == 7);
    terminal_clear();
    input = "cat\b\n";
    zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line);
    assert(!strcmp(buf, "ca")); expect(0, 'c', 12); expect(2, ' ', 7);
    terminal_clear();
    highlight_line("cat cat > cat && ./app # hi");
    expect(4, 'c', 7); expect(10, 'c', 7); expect(17, '.', 13);
    terminal_clear();
    terminal_writestring("\033[25;1HPROMPT> ");
    char long_input[130];
    memset(long_input, 'x', 100);
    long_input[100] = '\b'; long_input[101] = '\n'; long_input[102] = 0;
    input = long_input;
    zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line);
    assert(strlen(buf) == 99);
    expect(22 * 80, 'P', 7);
    for (unsigned i = 0; i < 99; i++) expect(22 * 80 + 8 + i, 'x', 12);
    expect(22 * 80 + 107, ' ', 7);
    terminal_clear(); input = "cat\003";
    zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line);
    assert(!buf[0] && terminal_color == 7);
    input = "";
    assert(!zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line));
    input = "cat";
    assert(zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line) == buf);
    assert(!strcmp(buf, "cat"));
    terminal_clear(); input = "ct\202a\n";
    zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line);
    assert(!strcmp(buf, "cat"));
    terminal_clear(); input = "xcat\204\206\205!\202\b\n";
    zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line);
    assert(!strcmp(buf, "ca!"));
    terminal_clear(); input = "ca file\204\203\203\t\n";
    zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line);
    assert(!strcmp(buf, "cat file"));
    terminal_clear(); input = "draft\202\200\201!\n";
    zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line);
    assert(!strcmp(buf, "draf!t"));
    terminal_clear(); input = "\200\n";
    zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line);
    assert(!strcmp(buf, "draf!t"));
    char tiny[4];
    terminal_clear(); input = "abcd\204Z\206Z\n";
    zenos_readline_highlight(tiny, sizeof(tiny), complete, highlight_line);
    assert(!strcmp(tiny, "Zbc"));
    terminal_clear();
    terminal_writestring("\033[25;1HPROMPT> ");
    memset(long_input, 'x', 100);
    long_input[100] = ZENOS_KEY_HOME; long_input[101] = ZENOS_KEY_DELETE;
    long_input[102] = 'Y'; long_input[103] = ZENOS_KEY_END;
    long_input[104] = '\n'; long_input[105] = 0;
    input = long_input;
    zenos_readline_highlight(buf, sizeof(buf), complete, highlight_line);
    assert(strlen(buf) == 100 && buf[0] == 'Y');
    expect(22 * 80, 'P', 7); expect(22 * 80 + 8, 'Y', 12);
    readline_history_count = 0;
    for (unsigned i = 0; i < 40; i++) {
        char command[16]; snprintf(command, sizeof(command), "cmd%u", i);
        remember(command, strlen(command));
    }
    assert(readline_history_count == 32 && !strcmp(history[0], "cmd8"));
    return 0;
}
