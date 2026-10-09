#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int chdir(const char *path) {
    return zenos_sysret(zenos_syscall1(SYS_CHDIR, (intptr_t)path));
}

char *getcwd(char *buf, size_t size) {
    if (zenos_sysret(zenos_syscall3(SYS_GETCWD, (intptr_t)buf, (intptr_t)size, 0)) < 0) return NULL;
    return buf;
}
