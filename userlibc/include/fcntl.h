#ifndef _USERLIBC_FCNTL_H
#define _USERLIBC_FCNTL_H 1
#include <unistd.h>
#define F_GETFL 3
#define F_SETFL 4
#ifdef __cplusplus
extern "C" {
#endif
int fcntl(int fd, int command, ...);
#ifdef __cplusplus
}
#endif
#endif
