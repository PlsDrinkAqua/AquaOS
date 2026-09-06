#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int open(const char *path, int flags) {
    return zenos_sysret(zenos_syscall3(SYS_OPEN, (int)path, flags, 0));
}
