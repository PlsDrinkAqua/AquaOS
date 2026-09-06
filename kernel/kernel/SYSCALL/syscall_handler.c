#include <stdint.h>
#include <libk/stdio.h>
#include <libk/string.h>
#include <kernel/elf.h>
#include <kernel/fd.h>
#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <kernel/task.h>
#include <kernel/usercopy.h>
#include <kernel/user_heap.h>
#include <kernel/vmm.h>

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
    SYS_TASKINFO = 9,
    SYS_SBRK    = 10,
    SYS_YIELD   = 11,
    SYS_SLEEP   = 12,
    SYS_OPEN    = 13,
    SYS_CLOSE   = 14,
    SYS_LSEEK   = 15,
    SYS_READDIR = 16,
};

void syscall_handler(interrupt_frame_t *regs)
{
    task_reap_exited_children();

    switch (regs->eax) {
        case SYS_PUTCHAR: {
            char c = (char)regs->ebx;
            if (fd_write(1, &c, 1) < 0) {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = regs->ebx;
            break;
        }

        case SYS_WRITE: {
            const char *buf = (const char *)regs->ecx;
            uint32_t count = regs->edx;

            if (!vmm_user_range_valid(buf, count, false)) {
                regs->eax = (uint32_t)-1;
                break;
            }

            regs->eax = (uint32_t)fd_write((int)regs->ebx, buf, count);
            break;
        }

        case SYS_GETKEY:
            regs->eax = (uint32_t)keyboard_getchar();
            break;

        case SYS_READ: {
            char *buf = (char *)regs->ecx;
            uint32_t count = regs->edx;
            uint32_t read = 0;

            if (count == 0) {
                regs->eax = 0;
                break;
            }

            if (!vmm_user_range_valid(buf, count, true)) {
                regs->eax = (uint32_t)-1;
                break;
            }

            if (fd_type((int)regs->ebx) != FD_STDIN) {
                char chunk[256];

                while (read < count) {
                    uint32_t wanted = count - read;
                    if (wanted > sizeof(chunk)) wanted = sizeof(chunk);
                    int n = fd_read((int)regs->ebx, chunk, wanted);
                    if (n < 0) {
                        regs->eax = read ? read : (uint32_t)-1;
                        break;
                    }
                    if (n == 0) {
                        regs->eax = read;
                        break;
                    }
                    if (copy_to_user(buf + read, chunk, (size_t)n) < 0) {
                        regs->eax = (uint32_t)-1;
                        break;
                    }
                    read += (uint32_t)n;
                    regs->eax = read;
                }
                break;
            }

            while (read < count) {
                int c = keyboard_getchar();
                if (c < 0) {
                    if (read == 0) {
                        task_block_current_for_keyboard(regs, buf, count);
                        return;
                    }
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
            char path[TASK_IMAGE_PATH_BYTES];

            if (copy_string_from_user(path, sizeof(path),
                                      (const char *)regs->ebx) < 0 ||
                task_exec_current(path, regs) < 0) {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_SPAWN: {
            char path[TASK_IMAGE_PATH_BYTES];

            if (copy_string_from_user(path, sizeof(path),
                                      (const char *)regs->ebx) < 0 ||
                task_spawn_current(path, regs) < 0) {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_WAIT:
            task_wait_current(regs->ebx, regs);
            break;

        case SYS_TASKINFO:
            task_dump_current(regs);
            regs->eax = 0;
            break;

        case SYS_SBRK:
            regs->eax = (uint32_t)sys_sbrk((intptr_t)regs->ebx);
            break;

        case SYS_YIELD:
            task_yield_current(regs);
            break;

        case SYS_SLEEP:
            task_sleep_current(regs->ebx, regs);
            break;

        case SYS_OPEN: {
            char path[256];
            if (copy_string_from_user(path, sizeof(path),
                                      (const char *)regs->ebx) < 0) {
                regs->eax = (uint32_t)-1;
            } else {
                regs->eax = (uint32_t)fd_open(path, (int)regs->ecx);
            }
            break;
        }

        case SYS_CLOSE:
            regs->eax = (uint32_t)fd_close((int)regs->ebx);
            break;

        case SYS_LSEEK:
            regs->eax = (uint32_t)fd_seek((int)regs->ebx,
                                          (int32_t)regs->ecx,
                                          (int)regs->edx);
            break;

        case SYS_READDIR: {
            char name[EXT2_NAME_MAX + 1];
            char *user_name = (char *)regs->ecx;
            uint32_t name_size = regs->edx;
            int result;

            if (name_size == 0 || name_size > sizeof(name) ||
                !vmm_user_range_valid(user_name, name_size, true)) {
                regs->eax = (uint32_t)-1;
                break;
            }
            result = fd_read_dir((int)regs->ebx, name, name_size);
            if (result > 0 && copy_to_user(user_name, name, kstrlen(name) + 1) < 0) {
                result = -1;
            }
            regs->eax = (uint32_t)result;
            break;
        }

        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}
