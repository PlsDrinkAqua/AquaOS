#ifndef _ZENOS_SYSCALL_H
#define _ZENOS_SYSCALL_H 1
#include <stdint.h>

enum {
    SYS_PUTCHAR = 0,
    SYS_WRITE   = 1,
    SYS_GETKEY  = 2,
    SYS_READ    = 3,
    SYS_CLEAR   = 4,
    SYS_EXIT    = 5,
    SYS_EXEC    = 6,
    SYS_SPAWN   = 7,
    SYS_WAIT    = 8,
    SYS_TASKINFO = 9,
    SYS_SBRK    = 10,
    SYS_YIELD   = 11,
    SYS_SLEEP   = 12,
    SYS_OPEN    = 13,
    SYS_CLOSE   = 14,
    SYS_LSEEK   = 15,
    SYS_READDIR = 16,
    SYS_FORK    = 17,
    SYS_DUP     = 18,
    SYS_DUP2    = 19,
    SYS_PIPE    = 20,
    SYS_CHDIR   = 21,
    SYS_GETCWD  = 22,
    SYS_MKDIR   = 23,
    SYS_UNLINK  = 24,
    SYS_RMDIR   = 25,
    SYS_RENAME  = 26,
    SYS_FCNTL   = 27,
    SYS_TERMINAL_CONTROL = 28,
    SYS_PING_START = 29,
    SYS_PING_RESULT = 30,
};

#ifdef __cplusplus
extern "C" {
#endif

intptr_t zenos_syscall1(int num, intptr_t arg1);
intptr_t zenos_syscall3(int num, intptr_t arg1, intptr_t arg2, intptr_t arg3);

#ifdef __cplusplus
}
#endif

#endif
