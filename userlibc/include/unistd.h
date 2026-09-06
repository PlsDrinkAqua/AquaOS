#ifndef _USERLIBC_UNISTD_H
#define _USERLIBC_UNISTD_H 1

#include <stddef.h>
#include <stdint.h>

typedef int ssize_t;
typedef int off_t;

#define O_RDONLY 0
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#ifdef __cplusplus
extern "C" {
#endif

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int open(const char *path, int flags);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int readdir(int fd, char *name, size_t name_size);
int exec(const char *path);
int spawn(const char *path);
int wait(int pid);
void *sbrk(intptr_t increment);
int yield(void);
int sleep(unsigned ticks);

#ifdef __cplusplus
}
#endif

#endif
