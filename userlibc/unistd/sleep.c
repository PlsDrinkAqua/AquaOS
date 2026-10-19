#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int sleep(unsigned ticks) {
    return aquaos_sysret(aquaos_syscall1(SYS_SLEEP, (intptr_t)ticks));
}
