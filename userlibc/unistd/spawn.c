#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

#include "../stdio/internal.h"
int spawn(const char *path) {
    if (__aquaos_stdio_sync_inputs()<0) return -1;
    return aquaos_sysret(aquaos_syscall3(SYS_SPAWN, (intptr_t)path, (intptr_t)environ, 0));
}
