#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int pipe(int pipefd[2]) {
    return aquaos_sysret(aquaos_syscall1(SYS_PIPE, (intptr_t)pipefd));
}
