#include <stdint.h>
#include <stddef.h>

#include <libk/stdio.h>
#include <libk/string.h>

#include <kernel/elf.h>
#include <kernel/pmm.h>
#include <kernel/task.h>
#include <kernel/user_heap.h>
#include <kernel/vmm.h>

#define PAGE_SIZE 0x1000U
#define VMM_PRESENT  (1u << 0)
#define VMM_RW       (1u << 1)
#define VMM_USER     (1u << 2)

static task_t task_table[TASK_MAX];
static task_t *current_task;
static uint32_t next_pid = 1;

static void task_copy_path(char *dest, size_t dest_size, const char *path) {
    size_t i = 0;

    if (!dest || dest_size == 0) {
        return;
    }

    if (!path) {
        dest[0] = '\0';
        return;
    }

    while (path[i] && i + 1 < dest_size) {
        dest[i] = path[i];
        i++;
    }
    dest[i] = '\0';
}

static void task_set_image_path(task_t *task, const char *path) {
    if (!task) {
        return;
    }

    task_copy_path(task->image_path, sizeof(task->image_path), path);
}

static task_t *task_find_by_pid(uint32_t pid) {
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (task_table[i].state != TASK_UNUSED && task_table[i].pid == pid) {
            return &task_table[i];
        }
    }

    return NULL;
}

static task_t *task_alloc(void) {
    for (uint32_t i = 1; i < TASK_MAX; i++) {
        if (task_table[i].state == TASK_UNUSED) {
            kmemset(&task_table[i], 0, sizeof(task_table[i]));
            task_table[i].pid = next_pid++;
            task_table[i].state = TASK_UNUSED;
            task_table[i].user_stack_top = USER_STACK_TOP;
            return &task_table[i];
        }
    }

    return NULL;
}

static int task_prepare_user_stack(task_t *task) {
    uintptr_t stack_page = USER_STACK_TOP - PAGE_SIZE;

    if (!vmm_translate(stack_page)) {
        uint32_t stack_phys = pmm_alloc_frame();
        if (!stack_phys) {
            return -1;
        }

        if (vmm_map_page(stack_page, stack_phys, VMM_PRESENT | VMM_RW | VMM_USER) < 0) {
            pmm_free_frame(stack_phys);
            return -1;
        }
    }

    kmemset((void *)stack_page, 0, PAGE_SIZE);
    task->user_stack_top = USER_STACK_TOP;
    return 0;
}

static int task_load_image(task_t *task, const char *path) {
    elf_load_result_t res;
    char image_path[sizeof(task->image_path)];

    if (!task || !path) {
        return -1;
    }

    task_set_image_path(task, path);
    kmemcpy(image_path, task->image_path, sizeof(image_path));

    if (elf_load_from_file(image_path, &res) < 0) {
        return -1;
    }

    if (task_prepare_user_stack(task) < 0) {
        return -1;
    }

    user_heap_init();

    task->entry = res.entry;
    task->state = TASK_RUNNING;
    task->exit_status = 0;
    kmemcpy(task->image_path, image_path, sizeof(task->image_path));
    return 0;
}

void tasking_init(void) {
    kmemset(task_table, 0, sizeof(task_table));
    task_table[0].pid = next_pid++;
    task_table[0].state = TASK_UNUSED;
    task_table[0].user_stack_top = USER_STACK_TOP;
    current_task = &task_table[0];
}

task_t *task_current(void) {
    return current_task;
}

int task_start_init(const char *path, uint32_t *entry_out, uint32_t *stack_out) {
    if (!current_task) {
        tasking_init();
    }

    if (task_load_image(current_task, path) < 0) {
        return -1;
    }

    if (entry_out) {
        *entry_out = current_task->entry;
    }
    if (stack_out) {
        *stack_out = current_task->user_stack_top;
    }

    kprintf("task: pid=%u started %s entry=0x%x stack=0x%x\n",
            current_task->pid,
            current_task->image_path,
            current_task->entry,
            current_task->user_stack_top);

    return 0;
}

int task_exec_current(const char *path, syscall_registers_t *regs) {
    if (!current_task || !regs) {
        return -1;
    }

    if (task_load_image(current_task, path) < 0) {
        return -1;
    }

    regs->eip = current_task->entry;
    regs->useresp = current_task->user_stack_top;
    regs->eax = 0;

    kprintf("task: pid=%u exec %s entry=0x%x\n",
            current_task->pid,
            current_task->image_path,
            current_task->entry);

    return 0;
}

int task_spawn_current(const char *path, syscall_registers_t *regs) {
    task_t *parent = current_task;
    task_t *child;
    char image_path[TASK_IMAGE_PATH_BYTES];
    uintptr_t stack_page = USER_STACK_TOP - PAGE_SIZE;

    if (!parent || !regs || !path) {
        return -1;
    }

    task_copy_path(image_path, sizeof(image_path), path);

    child = task_alloc();
    if (!child) {
        return -1;
    }

    parent->saved_regs = *regs;
    parent->saved_regs.eax = child->pid;
    kmemcpy(parent->user_stack_snapshot, (const void *)stack_page, TASK_USER_STACK_BYTES);

    child->parent_pid = parent->pid;
    current_task = child;

    if (task_load_image(child, image_path) < 0) {
        child->state = TASK_UNUSED;
        current_task = parent;
        return -1;
    }

    regs->eip = child->entry;
    regs->useresp = child->user_stack_top;
    regs->eax = 0;

    kprintf("task: pid=%u spawned child pid=%u image=%s entry=0x%x\n",
            parent->pid,
            child->pid,
            child->image_path,
            child->entry);

    return 0;
}

int task_wait_current(uint32_t pid) {
    task_t *child;
    int status;

    if (!current_task || pid == 0) {
        return -1;
    }

    child = task_find_by_pid(pid);
    if (!child || child->parent_pid != current_task->pid) {
        return -1;
    }

    if (child->state != TASK_EXITED) {
        return -1;
    }

    status = child->exit_status;
    kmemset(child, 0, sizeof(*child));
    return status;
}

void task_exit_current(int status, syscall_registers_t *regs) {
    task_t *exiting;
    task_t *parent;
    uintptr_t stack_page = USER_STACK_TOP - PAGE_SIZE;

    if (!current_task || !regs) {
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    exiting = current_task;
    exiting->state = TASK_EXITED;
    exiting->exit_status = status;

    kprintf("\ntask: pid=%u exited from %s with status %d\n",
            exiting->pid,
            exiting->image_path,
            status);

    parent = task_find_by_pid(exiting->parent_pid);
    if (parent) {
        current_task = parent;

        if (task_load_image(parent, parent->image_path) < 0) {
            kprintf("task: failed to reload parent pid=%u image=%s\n",
                    parent->pid,
                    parent->image_path);
            for (;;) {
                __asm__ volatile ("hlt");
            }
        }

        kmemcpy((void *)stack_page, parent->user_stack_snapshot, TASK_USER_STACK_BYTES);
        *regs = parent->saved_regs;

        kprintf("task: resumed parent pid=%u image=%s\n",
                parent->pid,
                parent->image_path);
        return;
    }

    if (task_load_image(exiting, "/shell") < 0) {
        kprintf("task: failed to restart /shell\n");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    regs->eip = exiting->entry;
    regs->useresp = exiting->user_stack_top;
    regs->eax = 0;
}
