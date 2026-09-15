#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <libk/string.h>

#include <kernel/tty.h>

#include "vga.h"

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*) 0xC00B8000;

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;
#define TERMINAL_LINES 1025
static uint16_t lines[TERMINAL_LINES][80];
static size_t live_top;
static size_t history_count;
static size_t view_offset;

static void terminal_redraw(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t line = (live_top + TERMINAL_LINES - view_offset + y) % TERMINAL_LINES;
        kmemcpy(&terminal_buffer[y * VGA_WIDTH], lines[line],
                VGA_WIDTH * sizeof(uint16_t));
    }
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
    terminal_clear();
}

void terminal_clear(void) {
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
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

void terminal_putentryat(unsigned char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    lines[(live_top + y) % TERMINAL_LINES][x] = vga_entry(c, color);
    if (!view_offset) terminal_buffer[index] = vga_entry(c, color);
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
    if (c == '\n') {
        // Move to the next line and reset column.
        terminal_column = 0;
        terminal_row++;
    } else if (c == '\b') {
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
}

void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        terminal_putchar(data[i]);
    }
}

void terminal_writestring(const char* data) {
    terminal_write(data, kstrlen(data));
}
