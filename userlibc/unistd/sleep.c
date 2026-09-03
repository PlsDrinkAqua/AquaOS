#include <unistd.h>
#include <zenos/syscall.h>

int sleep(unsigned ticks) {
    return zenos_syscall1(SYS_SLEEP, (int)ticks);
}
