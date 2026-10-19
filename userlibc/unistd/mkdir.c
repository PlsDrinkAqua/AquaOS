#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int mkdir(const char *path) {
    return aquaos_sysret(aquaos_syscall1(SYS_MKDIR, (intptr_t)path));
}
