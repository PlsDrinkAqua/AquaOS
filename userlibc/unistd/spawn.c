#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int spawn(const char *path) {
    return zenos_sysret(zenos_syscall3(SYS_SPAWN, (intptr_t)path, (intptr_t)environ, 0));
}
