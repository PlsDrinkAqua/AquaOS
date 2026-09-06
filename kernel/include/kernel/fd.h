#ifndef _KERNEL_FD_H
#define _KERNEL_FD_H

#include <stddef.h>
#include <stdint.h>

#include <kernel/ext2_api.h>

#define TASK_FD_MAX 16

typedef enum {
    FD_NONE = 0,
    FD_STDIN,
    FD_STDOUT,
    FD_STDERR,
    FD_EXT2,
} fd_type_t;

typedef struct {
    fd_type_t type;
    ext2_file_t file;
} fd_entry_t;

struct task;

void fd_table_init(struct task *task);
void fd_table_close_all(struct task *task);
fd_type_t fd_type(int fd);
int fd_open(const char *path, int flags);
int fd_close(int fd);
int fd_read(int fd, void *buf, size_t count);
int fd_write(int fd, const void *buf, size_t count);
int fd_seek(int fd, int32_t offset, int whence);
int fd_read_dir(int fd, char *name, size_t name_size);

#endif
