#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int rmdir(const char *path) {
    return aquaos_sysret(aquaos_syscall1(SYS_RMDIR, (intptr_t)path));
}
