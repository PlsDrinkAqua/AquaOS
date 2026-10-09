#include <unistd.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int pipe(int pipefd[2]) {
    return zenos_sysret(zenos_syscall1(SYS_PIPE, (intptr_t)pipefd));
}
