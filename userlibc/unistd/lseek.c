#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

off_t lseek(int fd, off_t offset, int whence) {
    intptr_t result = aquaos_syscall3(SYS_LSEEK, fd, offset, whence);
    if (result < 0) { errno = (int)-result; return -1; }
    return result;
}
