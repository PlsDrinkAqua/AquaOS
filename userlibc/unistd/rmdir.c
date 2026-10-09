#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int rmdir(const char *path) {
    return zenos_sysret(zenos_syscall1(SYS_RMDIR, (intptr_t)path));
}
