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

static void check_colors(void) {
    static const unsigned colors[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    char sequence[64];
    for (unsigned i = 0; i < 8; i++) {
        terminal_clear();
        snprintf(sequence, sizeof(sequence), "\033[%umA\033[%umB\033[0;%umC", 30 + i, 90 + i, 40 + i);
        terminal_writestring(sequence);
        assert(screen[0] == vga_entry('A', colors[i]));
        assert(screen[1] == vga_entry('B', colors[i] | 8));
        assert(screen[2] == vga_entry('C', 7 | (colors[i] << 4)));
    }
    terminal_clear();
    terminal_writestring("\033[1;31;44mA\033[22mB\033[7mC\033[27mD\033[39;49mE\033[mF");
    const unsigned expected[] = {0x1c, 0x14, 0x41, 0x14, 7, 7};
    for (unsigned i = 0; i < 6; i++) assert(screen[i] == vga_entry('A' + i, expected[i]));
    terminal_clear();
    terminal_writestring("\033[3");
    terminal_writestring("2mG\033[38;2;255;0;0mH\033[999999mI");
    assert(screen[0] == vga_entry('G', 2));
    assert(screen[1] == vga_entry('H', 2));
    assert(screen[2] == vga_entry('I', 2));
    terminal_writestring("\033[0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;31mJ");
    assert(screen[3] == vga_entry('J', 2));
    terminal_writestring("\033[2;3H\033[41m\033[K");
    assert(screen[82] == vga_entry(' ', 0x42));
    terminal_writestring("\033[2J");
    assert(screen[0] == vga_entry(' ', 0x42));
    terminal_clear();
    terminal_writestring("\033[93mY\033[0m\n");
    for (unsigned i = 0; i < 30; i++) terminal_putchar('\n');
    terminal_scroll_view(100);
    assert(screen[0] == vga_entry('Y', 14));
    terminal_clear();
    assert(terminal_color == 7);
}

int main(void) {
    terminal_buffer = screen;
    check_colors();
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
