#ifndef _KERNEL_TTY_H
#define _KERNEL_TTY_H

#include <stddef.h>

void terminal_initialize(void);
void terminal_clear(void);
void terminal_refresh(void);
/* Nestable kernel output transaction; caller prevents task interleaving. */
void terminal_begin_update(void);
void terminal_end_update(void);
/* Positive rows browse older output; negative rows browse newer output. */
void terminal_scroll_view(int rows);
void terminal_follow_output(void);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_writestring(const char* data);

#endif
