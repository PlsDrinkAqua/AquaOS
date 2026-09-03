#ifndef _USERLIBC_STDLIB_H
#define _USERLIBC_STDLIB_H 1

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);

__attribute__((noreturn))
void exit(int status);

#ifdef __cplusplus
}
#endif

#endif
