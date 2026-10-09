#include <stdint.h>
#include <stdbool.h>
#include <kernel/pic.h>
#include <libk/stdio.h>
#include <kernel/interrupt_frame.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/task.h>

void interrupt_handler(interrupt_frame_t *regs)
{
    uint32_t int_num = regs->int_num;

    switch(int_num) {
        case 32:
            timer_isr(regs);
            break;
        case 33:
            // Call the keyboard interrupt service routine if int_num is 32
            keyboard_isr();
            break;
        case 46:  // IRQ14: Primary ATA
            break;
        case 47:  // IRQ15: Secondary ATA
            break;
        default:
            // For other interrupts, do nothing
            break;
    }
    
    PIC_sendEOI(int_num - 32); //Subtract 32 becuase of the offset
}

void exception_handler(interrupt_frame_t *regs) {
    uintptr_t faulting_address = 0;
    bool from_user;

    if (!regs) {
        __asm__ volatile ("cli");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    if (regs->int_num == 14) {
        __asm__ volatile ("mov %%cr2, %0" : "=r"(faulting_address));
    }

    from_user = (regs->cs & 3U) == 3U;
    kprintf("\nexception %u: ip=0x%lx err=0x%lx",
            (unsigned)regs->int_num, (unsigned long)regs->eip, (unsigned long)regs->dummy_error);
    if (regs->int_num == 14) {
        kprintf(" address=0x%lx", (unsigned long)faulting_address);
    }
    kprintf(" mode=%s\n", from_user ? "user" : "kernel");

    if (from_user) {
        if (regs->int_num == 14) {
            task_t *task = task_current();
            const char *access = (regs->dummy_error & (1U << 4)) ? "execute" :
                                 (regs->dummy_error & (1U << 1)) ? "write" :
                                                                  "read";
            const char *reason = (regs->dummy_error & 1U) ? "protection violation" :
                                                            "page not present";

            kprintf("Segmentation fault: pid=%u image=%s address=0x%lx access=%s (%s)\n",
                    task ? task->pid : 0,
                    task ? task->image_path : "unknown",
                    (unsigned long)faulting_address,
                    access,
                    reason);
        }
        task_exit_current(128 + (int)regs->int_num, regs);
        return;
    }

    kprintf("fatal kernel exception; system halted\n");
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
