#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int readdir(int fd, char *name, size_t name_size) {
    return zenos_sysret(zenos_syscall3(SYS_READDIR, fd, (intptr_t)name, (intptr_t)name_size));
}
