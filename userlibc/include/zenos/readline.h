#ifndef _ZENOS_READLINE_H
#define _ZENOS_READLINE_H 1

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns buf, or NULL on an error or EOF before any characters.
 * Supports cursor editing and 32 entries of process-local history (127 bytes
 * per entry). Uses AquaOS terminal cursor extensions for wrapped lines. */
char *zenos_readline(char *buf, size_t size);
/* Completion may append to the NUL-terminated line, respecting size. */
char *zenos_readline_complete(char *buf, size_t size,
                             void (*complete)(char *, size_t));
/* Render must print the line without moving the cursor except through its text,
 * and restore default colours. The buffer contains no terminal escapes. */
char *zenos_readline_highlight(char *buf, size_t size,
                              void (*complete)(char *, size_t),
                              void (*render)(const char *));

#ifdef __cplusplus
}
#endif

#endif
