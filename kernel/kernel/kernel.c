#include <libk/stdio.h>
#include <stdint.h>

#include <kernel/tty.h>
#include <kernel/framebuffer.h>
#include <kernel/layout.h>
#include <kernel/pic.h>
#include <kernel/irq.h>
#include <kernel/multiboot.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/kha.h>
#include <kernel/kmalloc.h>
#include <kernel/user_heap.h>
#include <kernel/ata.h>
#include <kernel/ext2.h>
#include <kernel/ext2_api.h>
#include <kernel/elf.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/pci.h>
#include <kernel/e1000.h>

#define VMM_PRESENT  (1<<0)
#define VMM_RW       (1<<1)
#define VMM_USER     (1<<2)
extern void jump_usermode(uintptr_t entry, uintptr_t user_stack_top);

int debug = 4;

void kernel_main(multiboot_info_t* mbd, uint32_t magic) {
    terminal_initialize();
    kprintf("AquaOS x86-64: long mode, LP64 userspace\n");

    kprintf("Initilizing GDT.................");
	gdt_install();
	kprintf("done \n");

	kprintf("Initilizing IDT.................");
	idt_install();
	kprintf("done \n");

	kprintf("Initilizing Global Constructor.................");
	__asm__ volatile ("call _init");
	kprintf("done \n");

	kprintf("Initilizing Physical Memory Manager.................");
	pmm_init(mbd , magic);
	kprintf("done \n");

	kprintf("Initilizing Virtual Memory Manager.................");
	vmm_init();

	kprintf("done \n");

	kprintf("Initilizing Kernel Heap Allocator.................");
	vmm_heap_init();
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC &&
        framebuffer_init((const void *)(KERNEL_DIRECT_BASE + (uintptr_t)mbd))) {
        terminal_refresh();
        kprintf("Display: RGB framebuffer, 80x25 console\n");
    }
	kprintf("done \n");

	kprintf("Initilizing PIC.................");
	PIC_remap(32, 40);
	// 屏蔽 ATA 主从中断（14 和 15）
    IRQ_set_mask(14);   // 屏蔽主 ATA（原 IRQ14）
    IRQ_set_mask(15);   // 屏蔽从 ATA（原 IRQ15）
	kprintf("done \n");

	kprintf("Initilizing EXT2 File System.................");
	ext2_init();
	kprintf("done \n");

    kprintf("Initilizing Timer.................");
    timer_init(100);
    kprintf("done \n");

	kprintf("Initilizing Task Manager.................");
	tasking_init();
	kprintf("done \n");

    pci_init();
    e1000_init();

        kprintf("Loading Init Task.................");
        uintptr_t user_entry = 0;
        uintptr_t user_stack = 0;
        int ret = task_start_init("/bin/shell", &user_entry, &user_stack);
        if (ret < 0) ret = task_start_init("/shell", &user_entry, &user_stack);
        if (ret < 0) {
            kprintf("failed to load /bin/shell\n");
            for (;;) {
                __asm__ volatile ("hlt");
            }
        }
        kprintf("\n");
        kprintf("done \n");

	// Enable interrupts globally.
	kprintf("Enabling interrupts.................");
	__asm__ volatile ("sti");
	// asm volatile ("1: jmp 1b");
	kprintf("done \n");
    kprintf("About to enter user mode...\n");
	kprintf("entry: 0x%lx, stack: 0x%lx\n", (unsigned long)user_entry, (unsigned long)user_stack);

	jump_usermode(user_entry, user_stack);

    kprintf("ERROR: returned from user mode switch!\n");
    while (1) {
        __asm__ volatile ("hlt");
    }
}
