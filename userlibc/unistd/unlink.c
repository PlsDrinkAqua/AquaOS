#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int unlink(const char *path) {
    return aquaos_sysret(aquaos_syscall1(SYS_UNLINK, (intptr_t)path));
}
