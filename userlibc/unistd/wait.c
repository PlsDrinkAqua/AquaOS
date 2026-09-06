#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int wait(int pid) {
    return zenos_sysret(zenos_syscall1(SYS_WAIT, pid));
}
