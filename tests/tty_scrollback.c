/* Run: cc -I kernel/include -idirafter libk/include tests/tty_scrollback.c
 *      -o /tmp/zenos-tty-scrollback && /tmp/zenos-tty-scrollback
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../kernel/arch/i386/tty.c"

void *kmemcpy(void *restrict dst, const void *restrict src, size_t count) {
    return memcpy(dst, src, count);
}

size_t kstrlen(const char *s) { return strlen(s); }

static uint16_t screen[80 * 25];

static void line(unsigned number) {
    char text[32];
    snprintf(text, sizeof(text), "%04u\n", number);
    terminal_writestring(text);
}

static void expect_top(unsigned number) {
    char text[5];
    snprintf(text, sizeof(text), "%04u", number);
    for (size_t i = 0; i < 4; i++) assert((screen[i] & 255) == text[i]);
}

int main(void) {
    terminal_buffer = screen;
    terminal_color = 7;
    terminal_clear();
    terminal_scroll_view(24);
    assert(view_offset == 0);
    for (unsigned i = 0; i < 60; i++) line(i);
    expect_top(36);
    terminal_scroll_view(24);
    expect_top(12);
    line(60);
    expect_top(12);
    terminal_scroll_view(-1);
    expect_top(13);
    terminal_follow_output();
    expect_top(37);
    for (unsigned i = 61; i < 1100; i++) line(i);
    expect_top(1076);
    terminal_scroll_view(2000);
    assert(view_offset == 1000);
    expect_top(76);
    line(1100);
    expect_top(77);
    terminal_scroll_view(-2000);
    expect_top(1077);
    terminal_clear();
    terminal_scroll_view(2000);
    assert(view_offset == 0 && history_count == 0);
    for (size_t i = 0; i < 80 * 25; i++) assert(screen[i] == vga_entry(' ', 7));
    for (size_t i = 0; i < 80; i++) terminal_putchar('x');
    assert(terminal_row == 1 && terminal_column == 0);
    terminal_writestring("ab\b");
    assert(screen[80] == vga_entry('a', 7));
    assert(screen[81] == vga_entry(' ', 7));
    puts("Terminal scrollback checks passed.");
}
