#ifndef _ZENOS_READLINE_H
#define _ZENOS_READLINE_H 1

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns buf, or NULL on an error or EOF before any characters. */
char *zenos_readline(char *buf, size_t size);
/* Completion may append to the NUL-terminated line, respecting size. */
char *zenos_readline_complete(char *buf, size_t size,
                             void (*complete)(char *, size_t));

#ifdef __cplusplus
}
#endif

#endif
