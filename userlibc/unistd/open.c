#include <unistd.h>
#include <zenos/syscall.h>

int open(const char *path, int flags) {
    return zenos_syscall3(SYS_OPEN, (int)path, flags, 0);
}
