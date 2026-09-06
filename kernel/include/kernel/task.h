#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include <stdint.h>
#include <kernel/fd.h>
#include <kernel/interrupt_frame.h>
#include <kernel/paging.h>

#define USER_STACK_TOP 0xBFFFE000U
#define TASK_KERNEL_STACK_BYTES 4096U
#define TASK_IMAGE_PATH_BYTES 64U
#define TASK_MAX 16

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_EXITED,
} task_state_t;

typedef struct task {
    uint32_t pid;
    task_state_t state;
    char image_path[TASK_IMAGE_PATH_BYTES];
    uint32_t entry;
    uint32_t user_stack_top;
    uint32_t kernel_stack_base;
    uint32_t kernel_stack_top;
    uint32_t heap_start;
    uint32_t heap_brk;
    uint32_t heap_limit;
    page_directory_t *address_space;
    uint32_t parent_pid;
    uint32_t waiting_for_pid;
    uint32_t wake_tick;
    uint32_t keyboard_read_buf;
    uint32_t keyboard_read_count;
    uint8_t waiting_for_keyboard;
    uint8_t wait_status_delivered;
    uint8_t detached;
    fd_entry_t fds[TASK_FD_MAX];
    interrupt_frame_t saved_regs;
    int exit_status;
} task_t;

void tasking_init(void);
task_t *task_current(void);
int task_start_init(const char *path, uint32_t *entry_out, uint32_t *stack_out);
int task_exec_current(const char *path, interrupt_frame_t *regs);
int task_spawn_current(const char *path, interrupt_frame_t *regs);
void task_yield_current(interrupt_frame_t *regs);
void task_preemption_disable(void);
void task_preemption_enable(void);
void task_preempt_current(interrupt_frame_t *regs);
int task_block_current_for_keyboard(interrupt_frame_t *regs, char *buf, uint32_t count);
int task_complete_keyboard_read(char c);
void task_sleep_current(uint32_t ticks, interrupt_frame_t *regs);
void task_wake_sleepers(uint32_t now);
void task_wait_current(uint32_t pid, interrupt_frame_t *regs);
void task_exit_current(int status, interrupt_frame_t *regs);
void task_dump_current(interrupt_frame_t *regs);
void task_reap_exited_children(void);

#endif
