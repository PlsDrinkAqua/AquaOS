#include <unistd.h>
#include <zenos/syscall.h>

int yield(void) {
    return zenos_syscall1(SYS_YIELD, 0);
}
