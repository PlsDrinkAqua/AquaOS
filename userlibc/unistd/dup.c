#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int dup(int oldfd) {
    return zenos_sysret(zenos_syscall1(SYS_DUP, oldfd));
}

int dup2(int oldfd, int newfd) {
    return zenos_sysret(zenos_syscall3(SYS_DUP2, oldfd, newfd, 0));
}
