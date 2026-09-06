#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

ssize_t read(int fd, void *buf, size_t count) {
    return zenos_sysret(zenos_syscall3(SYS_READ, fd, (int)buf, (int)count));
}
