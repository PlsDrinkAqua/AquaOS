#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int spawn(const char *path) {
    return zenos_sysret(zenos_syscall1(SYS_SPAWN, (intptr_t)path));
}
