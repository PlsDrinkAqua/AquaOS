#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

ssize_t read(int fd, void *buf, size_t count) {
    return aquaos_sysret(aquaos_syscall3(SYS_READ, fd, (intptr_t)buf, (intptr_t)count));
}
