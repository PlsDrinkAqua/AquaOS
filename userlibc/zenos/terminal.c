#include <zenos/syscall.h>
#include <zenos/terminal.h>

int zenos_terminal_clear(void) {
    return zenos_syscall1(SYS_CLEAR, 0);
}

int zenos_terminal_claim(void) {
    return zenos_syscall1(SYS_TERMINAL_CONTROL, 0);
}
int zenos_terminal_begin_job(void) {
    return zenos_syscall1(SYS_TERMINAL_CONTROL, 1);
}
int zenos_terminal_end_job(void) {
    return zenos_syscall1(SYS_TERMINAL_CONTROL, 2);
}
