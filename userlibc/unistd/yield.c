#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int yield(void) {
    return zenos_sysret(zenos_syscall1(SYS_YIELD, 0));
}
