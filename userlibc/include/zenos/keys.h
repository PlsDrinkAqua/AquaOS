#ifndef _ZENOS_KEYS_H
#define _ZENOS_KEYS_H 1

/* Single-byte keyboard events, outside the ASCII text range. */
enum {
    ZENOS_KEY_UP = 0x80,
    ZENOS_KEY_DOWN,
    ZENOS_KEY_LEFT,
    ZENOS_KEY_RIGHT,
    ZENOS_KEY_HOME,
    ZENOS_KEY_END,
    ZENOS_KEY_DELETE
};

#endif
