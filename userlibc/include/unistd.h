#ifndef _USERLIBC_UNISTD_H
#define _USERLIBC_UNISTD_H 1

#include <stddef.h>
#include <stdint.h>

typedef int ssize_t;

#ifdef __cplusplus
extern "C" {
#endif

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int exec(const char *path);
int spawn(const char *path);
int wait(int pid);
void *sbrk(intptr_t increment);

#ifdef __cplusplus
}
#endif

#endif
