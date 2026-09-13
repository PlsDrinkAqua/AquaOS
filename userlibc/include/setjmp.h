#ifndef _USERLIBC_SETJMP_H
#define _USERLIBC_SETJMP_H
/* i386 callee-saved registers, caller stack, return PC. The floating environment is not rolled back. */
typedef unsigned int jmp_buf[6];
#ifdef __cplusplus
extern "C" {
#endif
int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int value) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
