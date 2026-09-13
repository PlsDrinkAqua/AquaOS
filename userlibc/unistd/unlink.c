#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int unlink(const char *path) {
    return zenos_sysret(zenos_syscall1(SYS_UNLINK, (int)path));
}
