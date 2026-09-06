#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int sleep(unsigned ticks) {
    return zenos_sysret(zenos_syscall1(SYS_SLEEP, (int)ticks));
}
