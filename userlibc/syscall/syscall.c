#include <zenos/syscall.h>

intptr_t zenos_syscall1(int num, intptr_t arg1) {
    intptr_t ret;

    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
        : "memory"
    );

    return ret;
}

intptr_t zenos_syscall3(int num, intptr_t arg1, intptr_t arg2, intptr_t arg3) {
    intptr_t ret;

    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );

    return ret;
}
