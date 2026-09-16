#ifndef _KERNEL_KEYBOARD_H
#define _KERNEL_KEYBOARD_H 1

/* User input ABI: keep in sync with userlibc/include/zenos/keys.h. */
enum {
    KEY_UP = 0x80,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT
};

void keyboard_isr(void);
int keyboard_getchar(void);

#endif
