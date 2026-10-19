#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int readdir(int fd, char *name, size_t name_size) {
    return aquaos_sysret(aquaos_syscall3(SYS_READDIR, fd, (intptr_t)name, (intptr_t)name_size));
}
