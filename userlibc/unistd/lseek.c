#include <unistd.h>
#include <zenos/syscall.h>

off_t lseek(int fd, off_t offset, int whence) {
    return zenos_syscall3(SYS_LSEEK, fd, offset, whence);
}
