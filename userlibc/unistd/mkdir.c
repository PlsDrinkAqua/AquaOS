#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int mkdir(const char *path) {
    return zenos_sysret(zenos_syscall1(SYS_MKDIR, (int)path));
}
