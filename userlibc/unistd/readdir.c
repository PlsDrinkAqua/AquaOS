#include <unistd.h>
#include <zenos/syscall.h>

int readdir(int fd, char *name, size_t name_size) {
    return zenos_syscall3(SYS_READDIR, fd, (int)name, (int)name_size);
}
