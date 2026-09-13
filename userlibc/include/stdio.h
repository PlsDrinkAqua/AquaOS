#ifndef _USERLIBC_STDIO_H
#define _USERLIBC_STDIO_H 1

#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

int rename(const char *old_path, const char *new_path);

int putchar(int);
int getchar(void);
int puts(const char*);
int printf(const char*, ...);

#ifdef __cplusplus
}
#endif

#endif
