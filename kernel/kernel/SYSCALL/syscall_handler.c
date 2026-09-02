#include <stdint.h>
#include <libk/stdio.h>
#include <libk/string.h>
#include <kernel/elf.h>
#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <kernel/task.h>

enum {
    SYS_PUTCHAR = 0,
    SYS_WRITE   = 1,
    SYS_GETKEY  = 2,
    SYS_READ    = 3,
    SYS_CLEAR   = 4,
    SYS_EXIT    = 5,
    SYS_EXEC    = 6,
    SYS_SPAWN   = 7,
    SYS_WAIT    = 8,
};

void syscall_handler(syscall_registers_t *regs)
{
    switch (regs->eax) {
        case SYS_PUTCHAR:
            terminal_putchar(regs->ebx);
            regs->eax = regs->ebx;
            break;

        case SYS_WRITE: {
            if (regs->ebx != 1 && regs->ebx != 2) {
                regs->eax = (uint32_t)-1;
                break;
            }

            const char *buf = (const char *)regs->ecx;
            uint32_t count = regs->edx;

            for (uint32_t i = 0; i < count; i++) {
                terminal_putchar(buf[i]);
            }

            regs->eax = count;
            break;
        }

        case SYS_GETKEY:
            regs->eax = (uint32_t)keyboard_getchar();
            break;

        case SYS_READ: {
            if (regs->ebx != 0) {
                regs->eax = (uint32_t)-1;
                break;
            }

            char *buf = (char *)regs->ecx;
            uint32_t count = regs->edx;
            uint32_t read = 0;

            while (read < count) {
                int c = keyboard_getchar();
                if (c < 0) {
                    break;
                }

                buf[read++] = (char)c;
            }

            regs->eax = read;
            break;
        }

        case SYS_CLEAR:
            terminal_clear();
            regs->eax = 0;
            break;

        case SYS_EXIT:
            task_exit_current((int)regs->ebx, regs);
            break;

        case SYS_EXEC: {
            const char *path = (const char *)regs->ebx;
            if (task_exec_current(path, regs) < 0) {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_SPAWN: {
            const char *path = (const char *)regs->ebx;
            if (task_spawn_current(path, regs) < 0) {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_WAIT:
            regs->eax = (uint32_t)task_wait_current(regs->ebx);
            break;

        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}
