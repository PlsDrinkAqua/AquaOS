#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

#include "../stdio/internal.h"
int fork(void) {
    if (__aquaos_stdio_sync_inputs()<0) return -1;
    return aquaos_sysret(aquaos_syscall1(SYS_FORK, 0));
}
