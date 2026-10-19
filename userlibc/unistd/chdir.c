#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int chdir(const char *path) {
    return aquaos_sysret(aquaos_syscall1(SYS_CHDIR, (intptr_t)path));
}

char *getcwd(char *buf, size_t size) {
    if (aquaos_sysret(aquaos_syscall3(SYS_GETCWD, (intptr_t)buf, (intptr_t)size, 0)) < 0) return NULL;
    return buf;
}
