#include <stdint.h>
#include <aquaos/display.h>
#include <kernel/framebuffer.h>
#include <kernel/kmalloc.h>
#include <kernel/net.h>
#include <aquaos/net_abi.h>
#include <kernel/timer.h>
#include <libk/stdio.h>
#include <libk/string.h>
#include <kernel/elf.h>
#include <kernel/ext2_api.h>
#include <kernel/errno.h>
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
    SYS_FORK    = 17,
    SYS_DUP     = 18,
    SYS_DUP2    = 19,
    SYS_PIPE    = 20,
    SYS_CHDIR   = 21,
    SYS_GETCWD  = 22,
    SYS_MKDIR   = 23,
    SYS_UNLINK  = 24,
    SYS_RMDIR   = 25,
    SYS_RENAME  = 26,
    SYS_FCNTL   = 27,
    SYS_TERMINAL_CONTROL = 28,
    SYS_PING_START = 29,
    SYS_PING_RESULT = 30,
    SYS_UDP_SOCKET = 31,
    SYS_UDP_BIND = 32,
    SYS_UDP_SEND = 33,
    SYS_UDP_RECEIVE = 34,
    SYS_TCP_SOCKET = 35,
    SYS_TCP_CONNECT = 36,
    SYS_TCP_STATUS = 37,
    SYS_TCP_SHUTDOWN = 38,
    SYS_NET_INFO = 39,
    SYS_DISPLAY_INFO = 40,
    SYS_DISPLAY_SHOW = 41,
    SYS_DISPLAY_RESTORE = 42,
};

void syscall_handler(interrupt_frame_t *regs)
{
    task_reap_exited_children();

    switch (regs->eax) {
        case SYS_DISPLAY_INFO: {
            aquaos_display_info info;
            int result=framebuffer_info(&info.width,&info.height) ?
                (copy_to_user((void *)regs->ebx,&info,sizeof(info))<0 ? -K_EFAULT : 0) : -K_ENODEV;
            regs->eax=(uintptr_t)result;
            break;
        }
        case SYS_DISPLAY_SHOW: {
            aquaos_display_request r;
            int result;
            if (!task_terminal_can_read(task_current())) { regs->eax=(uintptr_t)-K_EACCES; break; }
            if (copy_from_user(&r,(void *)regs->ebx,sizeof(r))<0) { regs->eax=(uintptr_t)-K_EFAULT; break; }
            /* Bound allocations and multiplication before touching user pixels. */
            if (!r.width || !r.height || r.width>2048 || r.height>2048 ||
                (uint64_t)r.width*r.height>1048576) { regs->eax=(uintptr_t)-K_EINVAL; break; }
            size_t bytes=(size_t)r.width*r.height*4;
            if (!vmm_user_range_valid((void *)(uintptr_t)r.pixels,bytes,false)) { regs->eax=(uintptr_t)-K_EFAULT; break; }
            uint32_t *snapshot=kmalloc(bytes);
            if(!snapshot) { regs->eax=(uintptr_t)-K_ENOMEM; break; }
            result=copy_from_user(snapshot,(void *)(uintptr_t)r.pixels,bytes)<0 ? -K_EFAULT : 0;
            if(result>=0) {
                task_preemption_disable();
                result=framebuffer_show(task_current()->pid,snapshot,r.width,r.height,r.x,r.y);
                task_preemption_enable();
            }
            kfree(snapshot);
            regs->eax=(uintptr_t)result;
            break;
        }
        case SYS_DISPLAY_RESTORE:
            task_preemption_disable();
            framebuffer_release(task_current()->pid);
            task_preemption_enable();
            regs->eax=0;
            break;

        case SYS_PUTCHAR: {
            char c = (char)regs->ebx;
            int result = fd_write(1, &c, 1);
            if (result < 0) {
                regs->eax = (uintptr_t)result;
                break;
            }
            regs->eax = regs->ebx;
            break;
        }

        case SYS_WRITE: {
            const char *buf = (const char *)regs->ecx;
            size_t count = regs->edx;
            if (count > 0x7fffffffU) { regs->eax = (uintptr_t)-K_EINVAL; break; }
            uint32_t written = 0;
            char chunk[256];

            if (!vmm_user_range_valid(buf, count, false)) {
                regs->eax = (uintptr_t)-K_EFAULT;
                break;
            }
            while (written < count) {
                uint32_t wanted = count - written;
                int n;
                if (wanted > sizeof(chunk)) wanted = sizeof(chunk);
                if (copy_from_user(chunk, buf + written, wanted) < 0) {
                    regs->eax = (uintptr_t)-K_EFAULT;
                    break;
                }
                n = fd_write((int)regs->ebx, chunk, wanted);
                if (n == -K_EAGAIN && written == 0 && fd_wait_object((int)regs->ebx)) {
                    task_block_current_for_pipe(regs, fd_wait_object((int)regs->ebx));
                    return;
                }
                if (n < 0) {
                    regs->eax = written ? written : (uintptr_t)n;
                    break;
                }
                written += (uintptr_t)n;
                regs->eax = written;
                if ((uintptr_t)n < wanted) break;
            }
            break;
        }

        case SYS_GETKEY:
            regs->eax = task_terminal_can_read(task_current()) ?
                (uintptr_t)keyboard_getchar() : (uintptr_t)-K_EIO;
            break;

        case SYS_READ: {
            char *buf = (char *)regs->ecx;
            size_t count = regs->edx;
            if (count > 0x7fffffffU) { regs->eax = (uintptr_t)-K_EINVAL; break; }
            uint32_t read = 0;

            if (count == 0) {
                regs->eax = 0;
                break;
            }

            if (!vmm_user_range_valid(buf, count, true)) {
                regs->eax = (uintptr_t)-K_EFAULT;
                break;
            }

            if (fd_type((int)regs->ebx) != FD_STDIN) {
                char chunk[256];

                while (read < count) {
                    uint32_t wanted = count - read;
                    if (wanted > sizeof(chunk)) wanted = sizeof(chunk);
                    int n = fd_read((int)regs->ebx, chunk, wanted);
                    if (n == -K_EAGAIN && read == 0 && fd_wait_object((int)regs->ebx)) {
                        task_block_current_for_pipe(regs, fd_wait_object((int)regs->ebx));
                        return;
                    }
                    if (n < 0) {
                        regs->eax = read ? read : (uintptr_t)n;
                        break;
                    }
                    if (n == 0) {
                        regs->eax = read;
                        break;
                    }
                    if (copy_to_user(buf + read, chunk, (size_t)n) < 0) {
                        regs->eax = (uintptr_t)-K_EFAULT;
                        break;
                    }
                    read += (uintptr_t)n;
                    regs->eax = read;
                }
                break;
            }

            if (!task_terminal_can_read(task_current())) {
                regs->eax = (uintptr_t)-K_EIO;
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

                if (c == 4) break; /* Consume terminal EOF without copying it. */

                if (copy_to_user(buf + read, &c, 1) < 0) {
                    regs->eax = (uintptr_t)-K_EFAULT;
                    break;
                }
                read++;
                /* Match blocked terminal reads; leave subsequent EOF queued. */
                break;
            }

            regs->eax = read;
            break;
        }

        case SYS_TERMINAL_CONTROL:
            regs->eax = (uintptr_t)task_terminal_control(regs->ebx);
            break;

        case SYS_NET_INFO: {
            aquaos_net_info info;
            net_get_info(&info);
            regs->eax = copy_to_user((void *)regs->ebx, &info, sizeof(info)) < 0
                ? (uintptr_t)-K_EFAULT : 0;
            break;
        }
        case SYS_TCP_SOCKET:
            regs->eax = (uintptr_t)fd_tcp_socket();
            break;
        case SYS_TCP_CONNECT:
            regs->eax = regs->ecx > UINT32_MAX || regs->edx > UINT16_MAX ?
                (uintptr_t)-K_EINVAL : (uintptr_t)fd_tcp_connect((int)regs->ebx, regs->ecx, regs->edx);
            break;
        case SYS_TCP_STATUS:
            regs->eax = (uintptr_t)fd_tcp_status((int)regs->ebx);
            break;
        case SYS_TCP_SHUTDOWN:
            regs->eax = (uintptr_t)fd_tcp_shutdown((int)regs->ebx);
            break;
        case SYS_UDP_SOCKET:
            regs->eax = (uintptr_t)fd_udp_socket();
            break;
        case SYS_UDP_BIND:
            regs->eax = regs->ecx > UINT16_MAX ? (uintptr_t)-K_EINVAL :
                (uintptr_t)fd_udp_bind((int)regs->ebx, (uint16_t)regs->ecx);
            break;
        case SYS_UDP_SEND:
        case SYS_UDP_RECEIVE: {
            int receiving = regs->eax == SYS_UDP_RECEIVE;
            aquaos_udp_request_t request;
            void *user_request = (void *)regs->ecx;
            int result = -K_EFAULT;
            uint8_t payload[NET_UDP_MAX_PAYLOAD];
            if (copy_from_user(&request, user_request, sizeof(request)) < 0) goto udp_done;
            if (request.reserved || (!receiving && request.length > NET_UDP_MAX_PAYLOAD)) {
                result = -K_EINVAL;
                goto udp_done;
            }
            if (receiving && request.length > NET_UDP_MAX_PAYLOAD)
                request.length = NET_UDP_MAX_PAYLOAD;
            void *data = (void *)(uintptr_t)request.data;
            if (!vmm_user_range_valid(data, request.length, receiving)) goto udp_done;
            if (receiving) {
                /* Validate all output memory before consuming a datagram. Syscall
                 * entry keeps IRQs disabled, so mappings cannot change here. */
                if (!vmm_user_range_valid((void *)regs->edx, sizeof(aquaos_udp_address), true)) goto udp_done;
                result = fd_udp_receive((int)regs->ebx, payload, request.length,
                                        &request.ip, &request.port);
                if (result >= 0) {
                    aquaos_udp_address source = {request.ip, request.port, 0};
                    if ((result && copy_to_user(data, payload, result) < 0) ||
                        copy_to_user((void *)regs->edx, &source, sizeof(source)) < 0)
                        result = -K_EFAULT;
                }
            } else {
                if (request.length && copy_from_user(payload, data, request.length) < 0) goto udp_done;
                result = fd_udp_send((int)regs->ebx, request.ip, request.port, payload, request.length);
            }
        udp_done:
            regs->eax = (uintptr_t)result;
            break;
        }

        case SYS_PING_START:
            regs->eax = regs->ebx > UINT32_MAX ? (uintptr_t)-K_EINVAL :
                (uintptr_t)net_ping_start(task_current()->pid, regs->ebx, timer_ticks());
            break;
        case SYS_PING_RESULT:
            regs->eax = (uintptr_t)net_ping_result(task_current()->pid);
            break;

        case SYS_CLEAR:
            terminal_clear();
            regs->eax = 0;
            break;

        case SYS_EXIT:
            task_exit_current((int)regs->ebx, regs);
            break;

        case SYS_RENAME: {
            char old_path[256], new_path[256], old_abs[256], new_abs[256];
            int result = -K_EFAULT;
            if (copy_string_from_user(old_path, sizeof(old_path), (const char *)regs->ebx) == 0 &&
                copy_string_from_user(new_path, sizeof(new_path), (const char *)regs->ecx) == 0) {
                result = task_resolve_path(old_path, old_abs, sizeof(old_abs));
                if (!result) result = task_resolve_path(new_path, new_abs, sizeof(new_abs));
                if (!result) {
                    task_preemption_disable();
                    result = ext2_rename(old_abs, new_abs);
                    task_preemption_enable();
                }
            }
            regs->eax = (uintptr_t)result;
            break;
        }

        case SYS_UNLINK:
        case SYS_RMDIR:
        case SYS_MKDIR: {
            char path[TASK_IMAGE_PATH_BYTES], absolute[TASK_IMAGE_PATH_BYTES];
            int result;
            if (copy_string_from_user(path, sizeof(path), (const char *)regs->ebx) < 0)
                result = -K_EFAULT;
            else {
                result = task_resolve_path(path, absolute, sizeof(absolute));
                if (result == 0) {
                    task_preemption_disable();
                    result = regs->eax == SYS_MKDIR ? ext2_mkdir(absolute) :
                        ext2_remove(absolute, regs->eax == SYS_RMDIR);
                    task_preemption_enable();
                }
            }
            regs->eax = (uintptr_t)result;
            break;
        }

        case SYS_CHDIR: {
            char path[TASK_IMAGE_PATH_BYTES];
            if (copy_string_from_user(path, sizeof(path), (const char *)regs->ebx) < 0)
                regs->eax = (uintptr_t)-K_EFAULT;
            else regs->eax = (uintptr_t)task_chdir(path);
            break;
        }

        case SYS_GETCWD: {
            const char *cwd = task_current()->cwd;
            size_t length = kstrlen(cwd) + 1;
            if (regs->ecx < length) regs->eax = (uintptr_t)-K_ERANGE;
            else if (copy_to_user((void *)regs->ebx, cwd, length) < 0)
                regs->eax = (uintptr_t)-K_EFAULT;
            else regs->eax = 0;
            break;
        }

        case SYS_EXEC: {
            char path[TASK_IMAGE_PATH_BYTES];

            if (copy_string_from_user(path, sizeof(path), (const char *)regs->ebx) < 0)
                regs->eax = (uintptr_t)-K_EFAULT;
            else {
                int result = task_exec_current(path, (const char *const *)regs->ecx, (const char *const *)regs->edx, regs);
                if (result < 0) regs->eax = (uintptr_t)result;
            }
            break;
        }

        case SYS_SPAWN: {
            char path[TASK_IMAGE_PATH_BYTES];

            if (copy_string_from_user(path, sizeof(path), (const char *)regs->ebx) < 0)
                regs->eax = (uintptr_t)-K_EFAULT;
            else {
                int result = task_spawn_current(path, regs);
                if (result < 0) regs->eax = (uintptr_t)result;
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
            regs->eax = (uintptr_t)sys_sbrk((intptr_t)regs->ebx);
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
                regs->eax = (uintptr_t)-K_EFAULT;
            } else {
                regs->eax = (uintptr_t)fd_open(path, (int)regs->ecx);
            }
            break;
        }

        case SYS_FCNTL:
            regs->eax = (uintptr_t)fd_fcntl((int)regs->ebx, (int)regs->ecx, (int)regs->edx);
            break;

        case SYS_CLOSE:
            regs->eax = (uintptr_t)fd_close((int)regs->ebx);
            break;

        case SYS_LSEEK:
            regs->eax = (uintptr_t)fd_seek((int)regs->ebx,
                                          (int64_t)regs->ecx,
                                          (int)regs->edx);
            break;

        case SYS_READDIR: {
            char name[EXT2_NAME_MAX + 1];
            char *user_name = (char *)regs->ecx;
            size_t name_size = regs->edx;
            int result;

            if (name_size == 0 || name_size > sizeof(name) ||
                !vmm_user_range_valid(user_name, name_size, true)) {
                regs->eax = (uintptr_t)-K_EFAULT;
                break;
            }
            result = fd_read_dir((int)regs->ebx, name, name_size);
            if (result > 0 && copy_to_user(user_name, name, kstrlen(name) + 1) < 0) {
                result = -K_EFAULT;
            }
            regs->eax = (uintptr_t)result;
            break;
        }

        case SYS_FORK:
            regs->eax = (uintptr_t)task_fork_current(regs);
            break;

        case SYS_DUP:
            regs->eax = (uintptr_t)fd_dup((int)regs->ebx);
            break;

        case SYS_DUP2:
            regs->eax = (uintptr_t)fd_dup2((int)regs->ebx, (int)regs->ecx);
            break;

        case SYS_PIPE: {
            int pair[2];
            int result;
            if (!vmm_user_range_valid((void *)regs->ebx, sizeof(pair), true)) {
                regs->eax = (uintptr_t)-K_EFAULT;
                break;
            }
            result = fd_pipe(pair);
            if (result == 0 && copy_to_user((void *)regs->ebx, pair, sizeof(pair)) < 0) {
                fd_close(pair[0]);
                fd_close(pair[1]);
                result = -K_EFAULT;
            }
            regs->eax = (uintptr_t)result;
            break;
        }

        default:
            regs->eax = (uintptr_t)-K_ENOSYS;
            break;
    }
}
