#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int open(const char *path, int flags, ...) {
    return aquaos_sysret(aquaos_syscall3(SYS_OPEN, (intptr_t)path, flags, 0));
}
