#ifndef _USERLIBC_ERRNO_H
#define _USERLIBC_ERRNO_H 1

extern int errno;

#define EPERM    1
#define ENOENT   2
#define ERANGE 34
#define ENAMETOOLONG 36
#define E2BIG    7
#define EIO      5
#define EBADF    9
#define ECHILD  10
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define EFAULT  14
#define EBUSY   16
#define EEXIST  17
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define ENFILE  23
#define EMFILE  24
#define EFBIG   27
#define ESPIPE  29
#define ENOSPC  28
#define EPIPE   32
#define ENOSYS  38

#endif
