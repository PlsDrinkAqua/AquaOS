#ifndef _KERNEL_FD_H
#define _KERNEL_FD_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/ext2_api.h>

#define TASK_FD_MAX 16

typedef enum {
    FD_NONE = 0, FD_STDIN, FD_STDOUT, FD_STDERR, FD_EXT2,
    FD_PIPE_READ, FD_PIPE_WRITE, FD_UDP, FD_TCP,
} fd_type_t;

typedef struct open_file open_file_t;
typedef struct { fd_type_t type; open_file_t *open; } fd_entry_t;
struct task;

void fd_table_init(struct task *task);
int fd_table_clone(struct task *dest, const struct task *source);
void fd_table_close_all(struct task *task);
fd_type_t fd_type(int fd);
void *fd_wait_object(int fd);
int fd_inode_is_open(uint32_t ino);
int fd_open(const char *path, int flags);
int fd_close(int fd);
int fd_fcntl(int fd, int command, int flags);
int fd_dup(int oldfd);
int fd_dup2(int oldfd, int newfd);
int fd_pipe(int pipefd[2]);
/* Nonblocking UDP operations; called with IRQs disabled. */
int fd_tcp_socket(void);
int fd_tcp_connect(int fd, uint32_t ip, uint16_t port);
int fd_tcp_status(int fd);
int fd_tcp_shutdown(int fd);
int fd_udp_socket(void);
int fd_udp_bind(int fd, uint16_t port);
int fd_udp_send(int fd, uint32_t ip, uint16_t port, const void *data, size_t length);
int fd_udp_receive(int fd, void *data, size_t capacity, uint32_t *ip, uint16_t *port);
int fd_read(int fd, void *buf, size_t count);
int fd_write(int fd, const void *buf, size_t count);
int64_t fd_seek(int fd, int64_t offset, int whence);
int fd_read_dir(int fd, char *name, size_t name_size);

#endif
