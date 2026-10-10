#ifndef _KERNEL_INTERRUPT_FRAME_H
#define _KERNEL_INTERRUPT_FRAME_H

#include <stdint.h>

/* Historical e-register names hold full native-width registers on x86-64.
 * Keep field order synchronized with the architecture interrupt stubs. */
typedef struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uintptr_t edi;
    uintptr_t esi;
    uintptr_t ebp;
    uintptr_t esp;
    uintptr_t ebx;
    uintptr_t edx;
    uintptr_t ecx;
    uintptr_t eax;
    uintptr_t int_num;
    uintptr_t dummy_error;
    uintptr_t eip;
    uintptr_t cs;
    uintptr_t eflags;
    uintptr_t useresp;
    uintptr_t ss;
} interrupt_frame_t;

#endif
