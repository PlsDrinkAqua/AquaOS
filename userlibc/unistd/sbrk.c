#include <stdint.h>
#include <unistd.h>
#include <aquaos/syscall.h>
#include <errno.h>

void *sbrk(intptr_t increment) {
    void *result = (void *)aquaos_syscall1(SYS_SBRK, (intptr_t)increment);
    if (result == (void *)-1) errno = ENOMEM;
    return result;
}
