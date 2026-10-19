#ifndef _USERLIBC_STDIO_H
#define _USERLIBC_STDIO_H 1
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define FOPEN_MAX 16

typedef struct aquaos_file FILE;
#ifdef __cplusplus
extern "C" {
#endif
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fileno(FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
int ungetc(int c, FILE *stream);
char *fgets(char *buffer, int size, FILE *stream);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int rename(const char *old_path, const char *new_path);
int remove(const char *path);
int putchar(int c);
int getchar(void);
int puts(const char *s);
int sscanf(const char *text, const char *format, ...);
int vsscanf(const char *text, const char *format, va_list args);
int printf(const char *format, ...);
int vprintf(const char *format, va_list args);
int fprintf(FILE *stream, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list args);
int sprintf(char *buffer, const char *format, ...);
int vsprintf(char *buffer, const char *format, va_list args);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
#ifdef __cplusplus
}
#endif
#endif
