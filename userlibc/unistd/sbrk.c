#include <stdint.h>
#include <unistd.h>
#include <zenos/syscall.h>
#include <errno.h>

void *sbrk(intptr_t increment) {
    void *result = (void *)zenos_syscall1(SYS_SBRK, (int)increment);
    if (result == (void *)-1) errno = ENOMEM;
    return result;
}
