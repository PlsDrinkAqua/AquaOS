#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include <stdint.h>
#include <kernel/paging.h>

#define USER_STACK_TOP 0xBFFFE000U
#define TASK_KERNEL_STACK_BYTES 4096U
#define TASK_IMAGE_PATH_BYTES 64U
#define TASK_MAX 16

typedef struct syscall_registers {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t int_num;
    uint32_t dummy_error;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t useresp;
    uint32_t ss;
} syscall_registers_t;

typedef enum {
    TASK_UNUSED = 0,
    TASK_RUNNING,
    TASK_EXITED,
} task_state_t;

typedef struct {
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
    syscall_registers_t saved_regs;
    int exit_status;
} task_t;

void tasking_init(void);
task_t *task_current(void);
int task_start_init(const char *path, uint32_t *entry_out, uint32_t *stack_out);
int task_exec_current(const char *path, syscall_registers_t *regs);
int task_spawn_current(const char *path, syscall_registers_t *regs);
int task_wait_current(uint32_t pid);
void task_exit_current(int status, syscall_registers_t *regs);
void task_dump_current(syscall_registers_t *regs);

#endif
