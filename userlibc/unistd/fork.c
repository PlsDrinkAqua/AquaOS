#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int fork(void) {
    return zenos_sysret(zenos_syscall1(SYS_FORK, 0));
}
