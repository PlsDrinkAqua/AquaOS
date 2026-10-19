#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int fork(void) {
    return aquaos_sysret(aquaos_syscall1(SYS_FORK, 0));
}
