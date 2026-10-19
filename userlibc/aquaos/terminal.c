#include <aquaos/syscall.h>
#include <aquaos/terminal.h>

int aquaos_terminal_clear(void) {
    return aquaos_syscall1(SYS_CLEAR, 0);
}

int aquaos_terminal_claim(void) {
    return aquaos_syscall1(SYS_TERMINAL_CONTROL, 0);
}
int aquaos_terminal_begin_job(void) {
    return aquaos_syscall1(SYS_TERMINAL_CONTROL, 1);
}
int aquaos_terminal_end_job(void) {
    return aquaos_syscall1(SYS_TERMINAL_CONTROL, 2);
}
