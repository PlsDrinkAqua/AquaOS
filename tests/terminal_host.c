#include <assert.h>
#include <string.h>

#include "../kernel/arch/i386/tty.c"

void *kmemcpy(void *restrict dst, const void *restrict src, size_t n) {
    return memcpy(dst, src, n);
}

size_t kstrlen(const char *s) { return strlen(s); }

int main(void) {
    uint16_t screen[2002];
    screen[0] = screen[2001] = 0x1234;
    terminal_buffer = screen + 1;
    terminal_clear();
    terminal_writestring("hello\033[2;3H\033[7mX\033[0mY");
    assert((screen[83] & 255) == 'X');
    assert((screen[83] >> 8) == 0x70);
    assert((screen[84] & 255) == 'Y');
    assert((screen[84] >> 8) == 0x07);
    terminal_writestring("\033[2;3H\033[K");
    assert((screen[83] & 255) == ' ');
    assert((screen[1] & 255) == 'h');
    terminal_writestring("\033[999999999999999999;99999999999999H");
    assert(terminal_row == 24 && terminal_column == 79);
    terminal_writestring("\033[0;0H");
    assert(terminal_row == 0 && terminal_column == 0);
    terminal_writestring("\033[12;");
    terminal_writestring("5H!");
    assert((screen[1 + 11 * 80 + 4] & 255) == '!');
    terminal_writestring("\033[2J");
    for (unsigned i = 1; i <= 2000; i++) assert((screen[i] & 255) == ' ');
    terminal_writestring("\033[Htext\033[7m\033[");
    terminal_clear();
    terminal_writestring("reset");
    assert((screen[1] & 255) == 'r' && (screen[1] >> 8) == 7);
    for (unsigned i = 0; i < 1100; i++) terminal_writestring("scroll\n");
    terminal_scroll_view(24);
    terminal_follow_output();
    assert(view_offset == 0 && terminal_row == 24);
    assert(screen[0] == 0x1234 && screen[2001] == 0x1234);
    return 0;
}
