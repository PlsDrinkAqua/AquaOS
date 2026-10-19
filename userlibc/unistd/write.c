#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

ssize_t write(int fd, const void *buf, size_t count) {
    return aquaos_sysret(aquaos_syscall3(SYS_WRITE, fd, (intptr_t)buf, (intptr_t)count));
}
