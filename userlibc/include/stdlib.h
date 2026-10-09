#ifndef _USERLIBC_STDLIB_H
#define _USERLIBC_STDLIB_H 1

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
float strtof(const char *text, char **end);
double strtod(const char *text, char **end);
long double strtold(const char *text, char **end);
int atoi(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
long long strtoll(const char *text, char **end, int base);
unsigned long long strtoull(const char *text, char **end, int base);
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);

__attribute__((noreturn))
void exit(int status);
__attribute__((noreturn))
void abort(void);

#ifdef __cplusplus
}
#endif

#endif
