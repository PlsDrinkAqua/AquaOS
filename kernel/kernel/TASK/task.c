#include <stdint.h>
#include <stddef.h>

#include <libk/stdio.h>
#include <libk/string.h>

#include <kernel/elf.h>
#include <kernel/kha.h>
#include <kernel/pmm.h>
#include <kernel/task.h>
#include <kernel/tss.h>
#include <kernel/user_heap.h>
#include <kernel/vmm.h>

#define PAGE_SIZE 0x1000U
#define VMM_PRESENT  (1u << 0)
#define VMM_RW       (1u << 1)
#define VMM_USER     (1u << 2)
#define USER_HEAP_LIMIT 0x80000000U

static task_t task_table[TASK_MAX];
static task_t *current_task;
static uint32_t next_pid = 1;

extern page_directory_t boot_page_directory;

#define KERNEL_VIRT_OFFSET 0xC0000000U

static const char *task_state_name(task_state_t state) {
    switch (state) {
        case TASK_UNUSED:
            return "UNUSED";
        case TASK_READY:
            return "READY";
        case TASK_RUNNING:
            return "RUNNING";
        case TASK_BLOCKED:
            return "BLOCKED";
        case TASK_EXITED:
            return "EXITED";
        default:
            return "UNKNOWN";
    }
}

static uint32_t kernel_vaddr_to_paddr(uint32_t vaddr) {
    if (vaddr >= KERNEL_VIRT_OFFSET) {
        return vaddr - KERNEL_VIRT_OFFSET;
    }

    return 0;
}

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

static uint32_t task_index(task_t *task) {
    return (uint32_t)(task - task_table);
}

static task_t *task_pick_next_ready(void) {
    uint32_t start = current_task ? task_index(current_task) + 1 : 0;

    for (uint32_t offset = 0; offset < TASK_MAX; offset++) {
        uint32_t index = (start + offset) % TASK_MAX;
        if (task_table[index].state == TASK_READY) {
            return &task_table[index];
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

static int task_alloc_kernel_stack(task_t *task) {
    if (!task) {
        return -1;
    }

    if (task->kernel_stack_base) {
        return 0;
    }

    void *stack = vmm_alloc_pages(TASK_KERNEL_STACK_BYTES / PAGE_SIZE,
                                  VMM_PRESENT | VMM_RW);
    if (!stack) {
        return -1;
    }

    kmemset(stack, 0, TASK_KERNEL_STACK_BYTES);
    task->kernel_stack_base = (uint32_t)stack;
    task->kernel_stack_top = task->kernel_stack_base + TASK_KERNEL_STACK_BYTES;
    return 0;
}

static void task_free_kernel_stack(task_t *task) {
    if (!task || !task->kernel_stack_base) {
        return;
    }

    vmm_free_pages((void *)task->kernel_stack_base,
                   TASK_KERNEL_STACK_BYTES / PAGE_SIZE);
    task->kernel_stack_base = 0;
    task->kernel_stack_top = 0;
}

static void task_switch_to(task_t *task) {
    current_task = task;
    vmm_switch_address_space(task->address_space);
    tss_set_kernel_stack(task->kernel_stack_top);
}

static int task_schedule(interrupt_frame_t *regs) {
    task_t *prev = current_task;
    task_t *next;

    if (!regs) {
        return -1;
    }

    if (prev && prev->state == TASK_RUNNING) {
        prev->saved_regs = *regs;
        prev->state = TASK_READY;
    }

    next = task_pick_next_ready();
    if (!next) {
        if (prev && prev->state == TASK_READY) {
            prev->state = TASK_RUNNING;
            return 0;
        }
        return -1;
    }

    next->state = TASK_RUNNING;
    task_switch_to(next);
    *regs = next->saved_regs;
    return 1;
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

static int task_load_image_into_space(task_t *task, const char *path, page_directory_t *pd) {
    elf_load_result_t res;
    char image_path[sizeof(task->image_path)];
    page_directory_t *old_pd;

    if (!task || !path || !pd) {
        return -1;
    }

    task_set_image_path(task, path);
    kmemcpy(image_path, task->image_path, sizeof(image_path));

    if (elf_load_from_file_into_address_space(image_path, pd, &res) < 0) {
        return -1;
    }

    old_pd = vmm_current_address_space();
    vmm_switch_address_space(pd);
    if (task_prepare_user_stack(task) < 0) {
        vmm_switch_address_space(old_pd);
        return -1;
    }
    vmm_switch_address_space(old_pd);

    task->entry = res.entry;
    task->heap_start = res.heap_start;
    task->heap_brk = res.heap_start;
    task->heap_limit = USER_HEAP_LIMIT;
    task->state = TASK_READY;
    task->exit_status = 0;
    kmemcpy(task->image_path, image_path, sizeof(task->image_path));
    return 0;
}

static int task_create_and_load_image(task_t *task, const char *path) {
    page_directory_t *pd;

    if (!task || !path) {
        return -1;
    }

    if (task_alloc_kernel_stack(task) < 0) {
        return -1;
    }

    pd = vmm_create_address_space();
    if (!pd) {
        task_free_kernel_stack(task);
        return -1;
    }

    if (task_load_image_into_space(task, path, pd) < 0) {
        vmm_destroy_address_space(pd);
        task_free_kernel_stack(task);
        return -1;
    }

    task->address_space = pd;
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

    if (task_create_and_load_image(current_task, path) < 0) {
        return -1;
    }

    task_switch_to(current_task);
    current_task->state = TASK_RUNNING;

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

int task_exec_current(const char *path, interrupt_frame_t *regs) {
    page_directory_t *old_pd;
    page_directory_t *new_pd;

    if (!current_task || !regs) {
        return -1;
    }

    old_pd = current_task->address_space;
    new_pd = vmm_create_address_space();
    if (!new_pd) {
        return -1;
    }

    if (task_load_image_into_space(current_task, path, new_pd) < 0) {
        vmm_destroy_address_space(new_pd);
        return -1;
    }

    current_task->address_space = new_pd;
    vmm_switch_address_space(new_pd);
    tss_set_kernel_stack(current_task->kernel_stack_top);
    if (old_pd) {
        vmm_destroy_address_space(old_pd);
    }

    regs->eip = current_task->entry;
    regs->useresp = current_task->user_stack_top;
    regs->eax = 0;
    current_task->state = TASK_RUNNING;

    kprintf("task: pid=%u exec %s entry=0x%x\n",
            current_task->pid,
            current_task->image_path,
            current_task->entry);

    return 0;
}

int task_spawn_current(const char *path, interrupt_frame_t *regs) {
    task_t *parent = current_task;
    task_t *child;
    char image_path[TASK_IMAGE_PATH_BYTES];

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

    child->parent_pid = parent->pid;

    if (task_create_and_load_image(child, image_path) < 0) {
        child->state = TASK_UNUSED;
        return -1;
    }

    child->saved_regs = *regs;
    child->saved_regs.eip = child->entry;
    child->saved_regs.useresp = child->user_stack_top;
    child->saved_regs.eax = 0;
    child->state = TASK_READY;
    regs->eax = child->pid;

    kprintf("task: pid=%u spawned child pid=%u image=%s entry=0x%x\n",
            parent->pid,
            child->pid,
            child->image_path,
            child->entry);

    return 0;
}

void task_yield_current(interrupt_frame_t *regs) {
    int switched = task_schedule(regs);
    if (switched <= 0 && regs) {
        regs->eax = 0;
    }
}

void task_preempt_current(interrupt_frame_t *regs) {
    task_schedule(regs);
}

int task_block_current_for_keyboard(interrupt_frame_t *regs) {
    if (!current_task || !regs) {
        return -1;
    }

    current_task->saved_regs = *regs;
    current_task->saved_regs.eax = 0;
    current_task->waiting_for_keyboard = 1;
    current_task->state = TASK_BLOCKED;

    if (task_schedule(regs) < 0) {
        current_task->waiting_for_keyboard = 0;
        current_task->state = TASK_RUNNING;
        return -1;
    }

    return 0;
}

void task_wake_keyboard_waiter(void) {
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        task_t *task = &task_table[i];

        if (task->state == TASK_BLOCKED && task->waiting_for_keyboard) {
            task->waiting_for_keyboard = 0;
            task->saved_regs.eax = 0;
            task->state = TASK_READY;
            return;
        }
    }
}

void task_wait_current(uint32_t pid, interrupt_frame_t *regs) {
    task_t *child;
    int status;

    if (!current_task || !regs || pid == 0) {
        if (regs) {
            regs->eax = (uint32_t)-1;
        }
        return;
    }

    child = task_find_by_pid(pid);
    if (!child || child->parent_pid != current_task->pid) {
        regs->eax = (uint32_t)-1;
        return;
    }

    if (child->state == TASK_EXITED) {
        status = child->exit_status;
        if (child->address_space) {
            vmm_destroy_address_space(child->address_space);
        }
        task_free_kernel_stack(child);
        kmemset(child, 0, sizeof(*child));
        regs->eax = (uint32_t)status;
        return;
    }

    current_task->saved_regs = *regs;
    current_task->waiting_for_pid = pid;
    current_task->state = TASK_BLOCKED;
    if (task_schedule(regs) < 0) {
        current_task->waiting_for_pid = 0;
        current_task->state = TASK_RUNNING;
        regs->eax = (uint32_t)-1;
    }
}

void task_reap_exited_children(void) {
    task_t *parent = current_task;

    if (!parent) {
        return;
    }

    for (uint32_t i = 0; i < TASK_MAX; i++) {
        task_t *child = &task_table[i];

        if (child == parent ||
            child->state != TASK_EXITED ||
            child->parent_pid != parent->pid ||
            !child->wait_status_delivered) {
            continue;
        }

        if (child->address_space) {
            vmm_destroy_address_space(child->address_space);
        }
        task_free_kernel_stack(child);
        kmemset(child, 0, sizeof(*child));
    }
}

void task_exit_current(int status, interrupt_frame_t *regs) {
    task_t *exiting;
    task_t *parent;
    bool parent_woken = false;

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
        if (parent->state == TASK_BLOCKED &&
            parent->waiting_for_pid == exiting->pid) {
            parent->saved_regs.eax = (uint32_t)status;
            parent->waiting_for_pid = 0;
            parent->state = TASK_READY;
            exiting->wait_status_delivered = 1;
            parent_woken = true;
        }

        if (task_schedule(regs) < 0) {
            if (parent_woken) {
                task_switch_to(parent);
                parent->state = TASK_RUNNING;
                *regs = parent->saved_regs;
                return;
            }

            for (;;) {
                __asm__ volatile ("hlt");
            }
        }
        return;
    }

    if (exiting->address_space) {
        page_directory_t *old_pd = exiting->address_space;
        exiting->address_space = NULL;
        vmm_switch_address_space(&boot_page_directory);
        vmm_destroy_address_space(old_pd);
    }

    if (task_create_and_load_image(exiting, "/shell") < 0) {
        kprintf("task: failed to restart /shell\n");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    task_switch_to(exiting);
    regs->eip = exiting->entry;
    regs->useresp = exiting->user_stack_top;
    regs->eax = 0;
}

void task_dump_current(interrupt_frame_t *regs) {
    task_t *task = current_task;
    uint32_t user_stack_base;
    uint32_t heap_start;
    uint32_t heap_brk;
    uint32_t heap_limit;
    uint32_t heap_top_vaddr;
    uint32_t heap_top_paddr;

    if (!task) {
        kprintf("taskinfo: no current task\n");
        return;
    }

    user_stack_base = task->user_stack_top - PAGE_SIZE;
    heap_start = user_heap_start_vaddr();
    heap_brk = user_heap_brk_vaddr();
    heap_limit = user_heap_limit_vaddr();
    heap_top_vaddr = heap_brk > heap_start ? heap_brk - 1 : heap_brk;
    heap_top_paddr = vmm_translate(heap_top_vaddr);

    kprintf("\n[taskinfo]\n");
    kprintf(" pid=%u parent=%u state=%s image=%s\n",
            task->pid,
            task->parent_pid,
            task_state_name(task->state),
            task->image_path);
    kprintf(" entry: vaddr=0x%x paddr=0x%x\n",
            task->entry,
            vmm_translate(task->entry));
    kprintf(" address_space: pd_vaddr=0x%x pd_paddr=0x%x\n",
            (uint32_t)task->address_space,
            kernel_vaddr_to_paddr((uint32_t)task->address_space));
    kprintf(" user stack: base=0x%x top=0x%x current_sp=0x%x current_sp_paddr=0x%x\n",
            user_stack_base,
            task->user_stack_top,
            regs ? regs->useresp : 0,
            regs ? vmm_translate(regs->useresp) : 0);
    kprintf(" kernel stack: base=0x%x top=0x%x esp0=0x%x\n",
            task->kernel_stack_base,
            task->kernel_stack_top,
            task->kernel_stack_top);
    kprintf(" heap: start=0x%x brk=0x%x limit=0x%x top_vaddr=0x%x top_paddr=0x%x\n",
            heap_start,
            heap_brk,
            heap_limit,
            heap_top_vaddr,
            heap_top_paddr);
    kprintf(" syscall frame: eip=0x%x eax=0x%x ebx=0x%x ecx=0x%x edx=0x%x\n",
            regs ? regs->eip : 0,
            regs ? regs->eax : 0,
            regs ? regs->ebx : 0,
            regs ? regs->ecx : 0,
            regs ? regs->edx : 0);
}
