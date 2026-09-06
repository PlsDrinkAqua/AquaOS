#include <unistd.h>
#include <zenos/syscall.h>

int close(int fd) {
    return zenos_syscall1(SYS_CLOSE, fd);
}
