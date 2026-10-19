#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int spawn(const char *path) {
    return aquaos_sysret(aquaos_syscall3(SYS_SPAWN, (intptr_t)path, (intptr_t)environ, 0));
}
