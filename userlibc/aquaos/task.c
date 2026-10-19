#include <aquaos/syscall.h>
#include <aquaos/task.h>

int aquaos_taskinfo(void) {
    return aquaos_syscall1(SYS_TASKINFO, 0);
}
