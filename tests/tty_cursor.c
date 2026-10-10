#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/kernel/TTY/tty.c"

void *kmemcpy(void *restrict dst, const void *restrict src, size_t n) {
    return memcpy(dst, src, n);
}
size_t kstrlen(const char *s) { return strlen(s); }
static uint16_t screen[80 * 25];

int main(void) {
    terminal_buffer = screen;
    cursor_enabled = true;
    terminal_clear();
    assert(screen[0] == vga_entry(' ', 0x70));
    assert(lines[0][0] == vga_entry(' ', 7));
    terminal_writestring("\033[95mAB\033[0m\033[1;2~");
    assert(screen[0] == vga_entry('A', 0x50));
    assert(lines[0][0] == vga_entry('A', 13));
    assert(!terminal_reverse && terminal_color == 7);
    terminal_writestring("\033[2;1~");
    assert(screen[0] == vga_entry('A', 13));
    assert(screen[1] == vga_entry('B', 0x50));
    terminal_writestring("X");
    assert(screen[1] == vga_entry('X', 7));
    terminal_writestring("\033[7m\033[H");
    assert(terminal_reverse && terminal_color == 0x70);
    terminal_writestring("\033[0m\033[25;80HZ");
    assert(terminal_row == 24 && terminal_column == 0);
    assert(screen[24 * 80] == vga_entry(' ', 0x70));
    terminal_scroll_view(1);
    assert(!cursor_drawn);
    assert(screen[0] == vga_entry('A', 13));
    terminal_follow_output();
    assert(cursor_drawn);
    terminal_clear();
    assert(screen[0] == vga_entry(' ', 0x70));
    for (size_t i = 1; i < 80 * 25; i++) assert(screen[i] == vga_entry(' ', 7));
    uint16_t before[80 * 25];
    memcpy(before, screen, sizeof(screen));
    terminal_writestring("\033[3~hello\b\033[95mX\033[1;3~");
    assert(!memcmp(before, screen, sizeof(screen)));
    terminal_writestring("\033[4~");
    assert(screen[0] == vga_entry('h', 7));
    assert(screen[2] == vga_entry('l', 0x70));
    assert(screen[4] == vga_entry('X', 13));
    terminal_writestring("\033[3~\033[25;80H!more");
    memcpy(before, screen, sizeof(screen));
    terminal_writestring("\b\b\033[H");
    assert(!memcmp(before, screen, sizeof(screen)));
    terminal_writestring("\033[4~");
    assert(!terminal_batch && cursor_row == 0 && cursor_column == 0);
    terminal_writestring("\033[3~");
    terminal_clear();
    assert(!terminal_batch);
    puts("Reverse cursor checks passed.");
    return 0;
}
