#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

off_t lseek(int fd, off_t offset, int whence) {
    return zenos_sysret(zenos_syscall3(SYS_LSEEK, fd, offset, whence));
}
