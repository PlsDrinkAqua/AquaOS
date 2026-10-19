#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int close(int fd) {
    return aquaos_sysret(aquaos_syscall1(SYS_CLOSE, fd));
}
