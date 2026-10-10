#ifndef _USERLIBC_SETJMP_H
#define _USERLIBC_SETJMP_H
/* Callee-saved registers, caller stack and return PC.
 * The floating-point environment is not rolled back. */
typedef unsigned long jmp_buf[8];
#ifdef __cplusplus
extern "C" {
#endif
int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int value) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
