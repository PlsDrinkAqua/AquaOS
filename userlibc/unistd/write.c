#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

ssize_t write(int fd, const void *buf, size_t count) {
    return zenos_sysret(zenos_syscall3(SYS_WRITE, fd, (int)buf, (int)count));
}
