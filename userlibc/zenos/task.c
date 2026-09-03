#include <zenos/syscall.h>
#include <zenos/task.h>

int zenos_taskinfo(void) {
    return zenos_syscall1(SYS_TASKINFO, 0);
}
