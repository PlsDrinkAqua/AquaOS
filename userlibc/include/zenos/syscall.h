#ifndef _ZENOS_SYSCALL_H
#define _ZENOS_SYSCALL_H 1

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
};

#ifdef __cplusplus
extern "C" {
#endif

int zenos_syscall1(int num, int arg1);
int zenos_syscall3(int num, int arg1, int arg2, int arg3);

#ifdef __cplusplus
}
#endif

#endif
