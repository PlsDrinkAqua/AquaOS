#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/layout.h>
#include <libk/string.h>

#include <kernel/tty.h>
#include <kernel/io.h>

#include "vga.h"

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*) (KERNEL_DIRECT_BASE + 0xB8000);

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;
#define TERMINAL_LINES 1025
static uint16_t lines[TERMINAL_LINES][80];
static size_t live_top;
static size_t history_count;
static size_t view_offset;
#define ESCAPE_ARGS 16
static unsigned escape_state, escape_arg[ESCAPE_ARGS], escape_index;
static bool escape_invalid;
static uint8_t terminal_fg = VGA_COLOR_LIGHT_GREY, terminal_bg;
static bool terminal_bold, terminal_reverse;
static bool cursor_enabled, cursor_drawn;
static bool terminal_batch;
static void terminal_redraw(void);
static size_t cursor_row, cursor_column;
void terminal_putentryat(unsigned char c, uint8_t color, size_t x, size_t y);

static void terminal_update_cursor(void) {
    if (terminal_batch || escape_state) return;
    if (cursor_drawn) {
        terminal_buffer[cursor_row * VGA_WIDTH + cursor_column] =
            lines[(live_top + cursor_row) % TERMINAL_LINES][cursor_column];
        cursor_drawn = false;
    }
    if (!cursor_enabled || view_offset) return;
    cursor_row = terminal_row;
    cursor_column = terminal_column;
    uint16_t cell = lines[(live_top + cursor_row) % TERMINAL_LINES][cursor_column];
    uint8_t color = cell >> 8;
    uint8_t reversed = ((color >> 4) & 7) | ((color & 7) << 4);
    /* Equal base colours need contrast, including bright black on black. */
    if ((reversed & 7) == ((reversed >> 4) & 7)) reversed ^= 0x70;
    terminal_buffer[cursor_row * VGA_WIDTH + cursor_column] =
        vga_entry((unsigned char)cell, reversed);
    cursor_drawn = true;
}

static void terminal_update_color(void) {
    uint8_t fg = terminal_fg | (terminal_bold ? 8 : 0);
    uint8_t bg = terminal_bg;
    /* VGA bit 7 controls blink, so backgrounds use the base eight colours. */
    terminal_color = terminal_reverse ? (bg | ((fg & 7) << 4))
                                      : (fg | (bg << 4));
}

static void terminal_reset_color(void) {
    terminal_fg = VGA_COLOR_LIGHT_GREY;
    terminal_bg = VGA_COLOR_BLACK;
    terminal_bold = terminal_reverse = false;
    terminal_update_color();
}

static void terminal_sgr(void) {
    static const uint8_t colors[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    for (unsigned i = 0; i <= escape_index; i++) {
        unsigned arg = escape_arg[i];
        if (arg == 0) terminal_reset_color();
        else if (arg == 1) terminal_bold = true;
        else if (arg == 22) terminal_bold = false;
        else if (arg == 7) terminal_reverse = true;
        else if (arg == 27) terminal_reverse = false;
        else if (arg == 39) terminal_fg = VGA_COLOR_LIGHT_GREY;
        else if (arg == 49) terminal_bg = VGA_COLOR_BLACK;
        else if (arg >= 30 && arg <= 37) terminal_fg = colors[arg - 30];
        else if (arg >= 40 && arg <= 47) terminal_bg = colors[arg - 40];
        else if (arg >= 90 && arg <= 97) terminal_fg = colors[arg - 90] | 8;
        else if (arg == 38 || arg == 48) {
            /* Skip unsupported indexed/RGB colours as a single attribute. */
            if (i + 1 <= escape_index && escape_arg[i + 1] == 5) i += 2;
            else if (i + 1 <= escape_index && escape_arg[i + 1] == 2) i += 4;
            else break;
        }
    }
    terminal_update_color();
}

/* Bounded CSI subset used by full-screen user programs. */
static int terminal_escape(unsigned char c) {
    if (!escape_state) {
        if (c != 27) return 0;
        escape_state = 1;
        return 1;
    }
    if (escape_state == 1) {
        escape_state = c == '[' ? 2 : 0;
        for (unsigned i = 0; i < ESCAPE_ARGS; i++) escape_arg[i] = 0;
        escape_index = 0;
        escape_invalid = false;
        return 1;
    }
    if (c >= '0' && c <= '9') {
        if (escape_arg[escape_index] < 1000)
            escape_arg[escape_index] = escape_arg[escape_index] * 10 + c - '0';
        else escape_invalid = true;
        return 1;
    }
    if (c == ';') {
        if (escape_index + 1 < ESCAPE_ARGS) escape_index++;
        else escape_invalid = true;
        return 1;
    }
    if (c < 0x40 || c > 0x7e) { escape_invalid = true; return 1; }
    escape_state = 0;
    if (escape_invalid) return 1;
    if (c == 'H' || c == 'f') {
        unsigned row = escape_arg[0] ? escape_arg[0] - 1 : 0;
        unsigned col = escape_arg[1] ? escape_arg[1] - 1 : 0;
        terminal_row = row < VGA_HEIGHT ? row : VGA_HEIGHT - 1;
        terminal_column = col < VGA_WIDTH ? col : VGA_WIDTH - 1;
    } else if (c == '~' && (escape_arg[0] == 1 || escape_arg[0] == 2)) {
        /* AquaOS linear movement: CSI 1;cells~ back, CSI 2;cells~ forward. */
        size_t pos = terminal_row * VGA_WIDTH + terminal_column;
        size_t amount = escape_arg[1];
        if (escape_arg[0] == 1) pos = amount > pos ? 0 : pos - amount;
        else pos += amount;
        if (pos >= VGA_WIDTH * VGA_HEIGHT) pos = VGA_WIDTH * VGA_HEIGHT - 1;
        terminal_row = pos / VGA_WIDTH;
        terminal_column = pos % VGA_WIDTH;
    } else if (c == '~' && escape_arg[0] == 3) {
        terminal_batch = true;
    } else if (c == '~' && escape_arg[0] == 4) {
        terminal_batch = false;
        terminal_redraw();
    } else if (c == 'J' && escape_arg[0] == 2) {
        for (size_t y = 0; y < VGA_HEIGHT; y++)
            for (size_t x = 0; x < VGA_WIDTH; x++)
                terminal_putentryat(' ', terminal_color, x, y);
    } else if (c == 'K' && escape_arg[0] == 0) {
        for (size_t x = terminal_column; x < VGA_WIDTH; x++)
            terminal_putentryat(' ', terminal_color, x, terminal_row);
    } else if (c == 'm') {
        terminal_sgr();
    }
    return 1;
}

static void terminal_redraw(void) {
    if (terminal_batch) return;
    cursor_drawn = false;
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t line = (live_top + TERMINAL_LINES - view_offset + y) % TERMINAL_LINES;
        kmemcpy(&terminal_buffer[y * VGA_WIDTH], lines[line],
                VGA_WIDTH * sizeof(uint16_t));
    }
    terminal_update_cursor();
}

void terminal_scroll_view(int rows) {
    if (rows > 0) {
        size_t amount = (size_t)rows;
        size_t available = history_count - view_offset;
        view_offset += amount < available ? amount : available;
    } else if (rows < 0) {
        size_t amount = (size_t)(-(rows + 1)) + 1;
        view_offset -= amount < view_offset ? amount : view_offset;
    }
    terminal_redraw();
}

void terminal_follow_output(void) {
    if (view_offset) {
        view_offset = 0;
        terminal_redraw();
    }
}

void terminal_initialize(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = VGA_MEMORY;
    outb(0x3d4, 0x0a);
    outb(0x3d5, 0x20);
    cursor_enabled = true;
    terminal_clear();
}

void terminal_clear(void) {
    terminal_batch = false;
    cursor_drawn = false;
    escape_state = 0;
    terminal_reset_color();
    live_top = 0;
    history_count = 0;
    view_offset = 0;
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
            lines[y][x] = terminal_buffer[index];
        }
    }
    terminal_row = 0;
    terminal_column = 0;
    terminal_update_cursor();
}

void terminal_setcolor(uint8_t color) {
    terminal_fg = color & 15;
    terminal_bg = (color >> 4) & 7;
    terminal_bold = terminal_reverse = false;
    terminal_color = color;
}

void terminal_putentryat(unsigned char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    lines[(live_top + y) % TERMINAL_LINES][x] = vga_entry(c, color);
    if (!view_offset && !terminal_batch) terminal_buffer[index] = vga_entry(c, color);
}

// Scrolls the terminal up by one line, and clears the last line.
static void terminal_scroll(void) {
    live_top = (live_top + 1) % TERMINAL_LINES;
    if (history_count < TERMINAL_LINES - VGA_HEIGHT) history_count++;
    // Keep the same history visible while output arrives, until it expires.
    if (view_offset && view_offset < history_count) view_offset++;
    // Clear the last row
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        terminal_putentryat(' ', terminal_color, x, VGA_HEIGHT - 1);
    }
    // Adjust the row counter to the last row.
    terminal_row = VGA_HEIGHT - 1;
    terminal_redraw();
}

void terminal_putchar(char c) {
    if (terminal_escape((unsigned char)c)) { terminal_update_cursor(); return; }
    if (c == '\t') {
        size_t spaces = 4 - terminal_column % 4;
        while (spaces--) terminal_putchar(' ');
        return;
    } else if (c == '\r') {
        terminal_column = 0;
    } else if (c == '\n') {
        // Move to the next line and reset column.
        terminal_column = 0;
        terminal_row++;
    } else if (c == '\b') {
        if (terminal_column == 0 && terminal_row > 0) {
            terminal_row--;
            terminal_column = VGA_WIDTH;
        }
        if (terminal_column > 0) {
            terminal_column--;
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        }
    } else {
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
        terminal_column++;
        // Wrap the line if needed.
        if (terminal_column >= VGA_WIDTH) {
            terminal_column = 0;
            terminal_row++;
        }
    }

    // If we have exceeded the screen height, scroll the screen.
    if (terminal_row >= VGA_HEIGHT) {
        terminal_scroll();
    }
    terminal_update_cursor();
}

void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        terminal_putchar(data[i]);
    }
}

void terminal_writestring(const char* data) {
    terminal_write(data, kstrlen(data));
}
