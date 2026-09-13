#include <string.h>

char *strcpy(char *dest, const char *src) {
    char *result = dest;
    while ((*dest++ = *src++)) {}
    return result;
}

char *strchr(const char *s, int c) {
    do { if (*s == (char)c) return (char *)s; } while (*s++);
    return NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    do { if (*s == (char)c) last = s; } while (*s++);
    return (char *)last;
}

char *strstr(const char *s, const char *needle) {
    if (!*needle) return (char *)s;
    for (; *s; s++) {
        const char *a = s, *b = needle;
        while (*b && *a == *b) { a++; b++; }
        if (!*b) return (char *)s;
    }
    return NULL;
}
