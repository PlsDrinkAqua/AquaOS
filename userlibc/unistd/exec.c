#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int exec(const char *path, const char *const argv[]) {
    return zenos_sysret(zenos_syscall3(SYS_EXEC, (intptr_t)path, (intptr_t)argv, 0));
}
