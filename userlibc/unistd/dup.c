#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int dup(int oldfd) {
    return aquaos_sysret(aquaos_syscall1(SYS_DUP, oldfd));
}

int dup2(int oldfd, int newfd) {
    return aquaos_sysret(aquaos_syscall3(SYS_DUP2, oldfd, newfd, 0));
}
