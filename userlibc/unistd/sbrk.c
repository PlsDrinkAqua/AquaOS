#include <stdint.h>
#include <unistd.h>
#include <zenos/syscall.h>

void *sbrk(intptr_t increment) {
    return (void *)zenos_syscall1(SYS_SBRK, (int)increment);
}
