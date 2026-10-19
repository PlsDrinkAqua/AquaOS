#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int yield(void) {
    return aquaos_sysret(aquaos_syscall1(SYS_YIELD, 0));
}
