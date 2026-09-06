#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int close(int fd) {
    return zenos_sysret(zenos_syscall1(SYS_CLOSE, fd));
}
