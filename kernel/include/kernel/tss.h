#ifndef TSS_H
#define TSS_H

#include <stdint.h>

/*
 * 32-bit x86 Task State Segment
 * We only really need ss0 and esp0 for privilege transitions
 * from ring3 to ring0, but the CPU-defined layout must be preserved.
 */
struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

extern struct tss_entry tss;

/*
 * Install the TSS into GDT entry num.
 *
 * num              : GDT index for the TSS descriptor (e.g. 5)
 * kernel_ss        : kernel data segment selector (e.g. 0x10)
 * kernel_esp       : initial kernel stack top
 */
void tss_install(int num, uint16_t kernel_ss, uint32_t kernel_esp);

/*
 * Update kernel stack top used when CPU switches from ring3 to ring0.
 * Call this whenever you switch to a different process/thread.
 */
void tss_set_kernel_stack(uintptr_t kernel_esp);

/*
 * Load task register (TR) with the TSS selector.
 */
void tss_flush(void);

#endif