#include <stdint.h>
#include <libk/string.h>

#include <kernel/tss.h>
#include <kernel/gdt.h>

/*
 * Adjust these if your selector layout changes.
 *
 * GDT layout assumed:
 *   0: null
 *   1: kernel code  -> 0x08
 *   2: kernel data  -> 0x10
 *   3: user code    -> 0x18
 *   4: user data    -> 0x20
 *   5: tss          -> 0x28
 */

struct tss_entry tss;

/*
 * We assume your gdt_set_gate() already exists and has this signature:
 *
 * void gdt_set_gate(int num, unsigned long base, unsigned long limit,
 *                   unsigned char access, unsigned char gran);
 *
 * For a 32-bit available TSS descriptor:
 *   access = 0x89
 *     10001001b
 *     P=1, DPL=00, S=0(system), Type=1001(32-bit available TSS)
 *
 *   gran = 0x00
 *     byte granularity, not page granularity
 *     big/long bits should be 0 for TSS
 */

void tss_install(int num, uint16_t kernel_ss, uint32_t kernel_esp)
{
    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = sizeof(struct tss_entry) - 1;

    /* Zero the whole TSS first */
    kmemset(&tss, 0, sizeof(struct tss_entry));

    /* Create TSS descriptor in GDT */
    gdt_set_gate(num, base, limit, 0x89, 0x00);

    /*
     * Fields used by CPU when privilege changes from ring3 -> ring0
     */
    tss.ss0  = kernel_ss;
    tss.esp0 = kernel_esp;

    /*
     * Optional but recommended:
     * iomap_base >= sizeof(TSS) means "no I/O permission bitmap"
     */
    tss.iomap_base = sizeof(struct tss_entry);
}

void tss_set_kernel_stack(uintptr_t kernel_esp)
{
    tss.esp0 = kernel_esp;
}

void tss_flush(void)
{
    /*
     * TSS is at GDT entry 5 -> selector 0x28
     * If you move it elsewhere, update this value.
     */
    __asm__ volatile (
        "movw $0x28, %%ax \n\t"
        "ltr %%ax         \n\t"
        :
        :
        : "ax", "memory"
    );
}