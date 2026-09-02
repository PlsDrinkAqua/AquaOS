#include <unistd.h>
#include <zenos/syscall.h>

int wait(int pid) {
    return zenos_syscall1(SYS_WAIT, pid);
}
