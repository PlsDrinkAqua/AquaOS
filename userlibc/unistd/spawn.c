#include <unistd.h>
#include <zenos/syscall.h>

int spawn(const char *path) {
    return zenos_syscall1(SYS_SPAWN, (int)path);
}
