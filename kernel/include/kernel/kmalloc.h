#ifndef _KMALLOC
#define _KMALLOC

#include <stddef.h>

/**
 * Allocate a block of at least `size` bytes (aligned to 8 bytes).
 * Returns a pointer to the usable memory, or NULL on failure.
 */
void *kmalloc(size_t size);

/**
 * Free a block previously returned by kmalloc().
 * If ptr is NULL, does nothing.
 */
void  kfree(void *ptr);

#endif
