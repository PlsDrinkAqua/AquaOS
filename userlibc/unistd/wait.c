#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int wait(int pid) {
    return aquaos_sysret(aquaos_syscall1(SYS_WAIT, pid));
}
