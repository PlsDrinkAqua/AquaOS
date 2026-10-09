#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int open(const char *path, int flags, ...) {
    return zenos_sysret(zenos_syscall3(SYS_OPEN, (intptr_t)path, flags, 0));
}
