#ifndef _AQUAOS_KEYS_H
#define _AQUAOS_KEYS_H 1

/* Single-byte keyboard events, outside the ASCII text range. */
enum {
    AQUAOS_KEY_UP = 0x80,
    AQUAOS_KEY_DOWN,
    AQUAOS_KEY_LEFT,
    AQUAOS_KEY_RIGHT,
    AQUAOS_KEY_HOME,
    AQUAOS_KEY_END,
    AQUAOS_KEY_DELETE
};

#endif
