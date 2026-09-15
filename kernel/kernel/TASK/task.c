#include <stdint.h>
#include <stddef.h>

#include <libk/stdio.h>
#include <libk/string.h>

#include <kernel/elf.h>
#include <kernel/ext2_api.h>
#include <kernel/errno.h>
#include <kernel/kha.h>
#include <kernel/kmalloc.h>
#include <kernel/pmm.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/tss.h>
#include <kernel/usercopy.h>
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
static uint32_t terminal_owner;
static uint32_t next_job_id = 1;
static uint32_t preempt_disable_depth;

extern page_directory_t boot_page_directory;

#define KERNEL_VIRT_OFFSET 0xC0000000U

static uint8_t initial_fpu[512] __attribute__((aligned(16)));
static task_t *fpu_owner;

static void fpu_init(void) {
    uint32_t a, b, c, d, cr;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    if ((d & ((1U << 24) | (1U << 25))) != ((1U << 24) | (1U << 25))) {
        kprintf("ZenOS requires FXSR and SSE for floating-point contexts\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr));
    cr = (cr & ~12U) | 34U; /* Clear EM/TS, enable MP/NE. */
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr) : "memory");
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr));
    cr |= (1U << 9) | (1U << 10);
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr) : "memory");
    uint32_t mxcsr = 0x1f80;
    __asm__ volatile ("fninit; ldmxcsr %0; xorps %%xmm0, %%xmm0; xorps %%xmm1, %%xmm1;"
                      "xorps %%xmm2, %%xmm2; xorps %%xmm3, %%xmm3; xorps %%xmm4, %%xmm4;"
                      "xorps %%xmm5, %%xmm5; xorps %%xmm6, %%xmm6; xorps %%xmm7, %%xmm7"
                      :: "m"(mxcsr) : "memory");
    __asm__ volatile ("fxsave %0" : "=m"(initial_fpu));
    fpu_owner = NULL;
}
static void fpu_reset(task_t *task) {
    kmemcpy(task->fpu_state, initial_fpu, sizeof(initial_fpu));
    if (fpu_owner == task) __asm__ volatile ("fxrstor %0" :: "m"(task->fpu_state));
}

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
        case TASK_SLEEPING:
            return "SLEEPING";
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

void task_preemption_disable(void) {
    preempt_disable_depth++;
    __asm__ volatile ("" ::: "memory");
}

void task_preemption_enable(void) {
    __asm__ volatile ("" ::: "memory");
    if (preempt_disable_depth > 0) {
        preempt_disable_depth--;
    }
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

/* Keep dot components for the filesystem to validate before canonicalizing. */
int task_resolve_path(const char *path, char *out, size_t size) {
    if (!path || !*path) return -K_ENOENT;
    const char *cwd = current_task && current_task->cwd[0] ? current_task->cwd : "/";
    size_t prefix = *path == '/' ? 0 : kstrlen(cwd);
    size_t length = kstrlen(path);
    if (prefix + (prefix ? 1 : 0) + length + 1 > size) return -K_ENAMETOOLONG;
    if (prefix) {
        kmemcpy(out, cwd, prefix);
        out[prefix++] = '/';
    }
    kmemcpy(out + prefix, path, length + 1);
    return 0;
}

/* Called while filesystem mutations are serialized. Compare inode identity,
 * so aliases involving repeated slashes or dot components cannot bypass this. */
int task_directory_in_use(uint32_t ino) {
    for (unsigned i = 0; i < TASK_MAX; i++) {
        if (task_table[i].state == TASK_UNUSED || task_table[i].state == TASK_EXITED) continue;
        uint32_t cwd_ino;
        int result = ext2_lookup_inode(task_table[i].cwd, &cwd_ino);
        if (result < 0) return result;
        if (cwd_ino == ino) return 1;
    }
    return 0;
}

static int task_chdir_locked(const char *path) {
    char absolute[TASK_IMAGE_PATH_BYTES];
    ext2_file_t file;
    int result = task_resolve_path(path, absolute, sizeof(absolute));
    if (result < 0) return result;
    result = ext2_file_open(absolute, 0, &file);
    if (result < 0) return result;
    if (!ext2_file_is_dir(&file)) return -K_ENOTDIR;

    char *p = absolute;
    char *out = current_task->cwd;
    size_t used = 1;
    out[0] = '/';
    while (*p) {
        while (*p == '/') p++;
        const char *start = p;
        while (*p && *p != '/') p++;
        size_t length = p - start;
        if (!length || (length == 1 && start[0] == '.')) continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            while (used > 1 && out[used - 1] != '/') used--;
            if (used > 1) used--;
        } else {
            if (used > 1) out[used++] = '/';
            kmemcpy(out + used, start, length);
            used += length;
        }
    }
    out[used] = 0;
    return 0;
}

int task_chdir(const char *path) {
    task_preemption_disable();
    int result = task_chdir_locked(path);
    task_preemption_enable();
    return result;
}

static void task_clear_wait_state(task_t *task) {
    if (!task) {
        return;
    }

    task->waiting_for_pid = 0;
    task->wake_tick = 0;
    task->keyboard_read_buf = 0;
    task->keyboard_read_count = 0;
    task->waiting_for_keyboard = 0;
    task->wait_status_delivered = 0;
    task->waiting_pipe = NULL;
}

static task_t *task_find_by_pid(uint32_t pid) {
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (task_table[i].state != TASK_UNUSED && task_table[i].pid == pid) {
            return &task_table[i];
        }
    }

    return NULL;
}

/* Terminal actions: claim a shell prompt, begin a job, finish a job.
 * The shell starts a job before fork so every pipeline stage inherits it. */
int task_terminal_control(unsigned action) {
    task_t *owner = task_find_by_pid(terminal_owner);
    if (!current_task) return -K_EPERM;
    if (action == 0) {
        if (owner == current_task) return 0;
        if (owner && (!owner->child_job_id ||
                      current_task->job_id != owner->child_job_id)) return -K_EPERM;
        current_task->terminal_parent = terminal_owner;
        terminal_owner = current_task->pid;
        return 0;
    }
    if (owner != current_task) return -K_EPERM;
    if (action == 1) {
        if (owner->child_job_id) return -K_EBUSY;
        owner->child_job_id = next_job_id++;
        if (!next_job_id) next_job_id = 1;
        owner->job_cancelled = 0;
        return 0;
    }
    if (action == 2) {
        owner->child_job_id = 0;
        owner->job_cancelled = 0;
        return 0;
    }
    return -K_EINVAL;
}

int task_terminal_can_read(const task_t *task) {
    task_t *owner = task_find_by_pid(terminal_owner);
    if (!owner) return 1;
    return task && (owner->child_job_id ? task->job_id == owner->child_job_id :
                                        task == owner);
}

/* IRQ context: mark only; never close files or abandon a kernel operation. */
int task_interrupt_foreground(void) {
    task_t *owner = task_find_by_pid(terminal_owner);
    if (!owner || !owner->child_job_id) return 0;
    owner->job_cancelled = 1;
    for (unsigned i = 0; i < TASK_MAX; i++) {
        task_t *task = &task_table[i];
        if (task->state == TASK_UNUSED || task->state == TASK_EXITED ||
            task->job_id != owner->child_job_id) continue;
        task->interrupt_pending = 1;
        if (task->state == TASK_BLOCKED || task->state == TASK_SLEEPING) {
            task_clear_wait_state(task);
            task->state = TASK_READY;
        }
    }
    return 1;
}

/* Called with interrupts disabled after the complete outer handler returns.
 * Scheduling may choose another interrupted member of the same pipeline. */
void task_return_to_user(interrupt_frame_t *regs) {
    if ((regs->cs & 3U) != 3U || preempt_disable_depth) return;
    while (current_task && current_task->interrupt_pending) {
        current_task->interrupt_pending = 0;
        task_exit_current(130, regs);
    }
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

static void task_idle_until_ready(void) {
    while (!task_pick_next_ready()) {
        __asm__ volatile ("sti; hlt" ::: "memory");
    }
}

static task_t *task_alloc(void) {
    for (uint32_t i = 1; i < TASK_MAX; i++) {
        if (task_table[i].state == TASK_UNUSED) {
            kmemset(&task_table[i], 0, sizeof(task_table[i]));
            task_table[i].pid = next_pid++;
            if (next_pid == 0) {
                next_pid = 1;
            }
            task_table[i].state = TASK_UNUSED;
            task_table[i].user_stack_top = USER_STACK_TOP;
            fd_table_init(&task_table[i]);
            fpu_reset(&task_table[i]);
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
    if (fpu_owner != task) {
        if (fpu_owner) __asm__ volatile ("fxsave %0" : "=m"(fpu_owner->fpu_state));
        __asm__ volatile ("fxrstor %0" :: "m"(task->fpu_state));
        fpu_owner = task;
    }
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

    /* An IRQ may request termination just before a syscall marks itself blocked. */
    if (prev && prev->interrupt_pending &&
        (prev->state == TASK_BLOCKED || prev->state == TASK_SLEEPING)) {
        task_clear_wait_state(prev);
        prev->state = TASK_READY;
    }

    next = task_pick_next_ready();
    if (!next) {
        if (prev && prev->state == TASK_READY) {
            prev->state = TASK_RUNNING;
            return 0;
        }

        task_idle_until_ready();
        next = task_pick_next_ready();
        if (!next) {
            return -1;
        }
    }

    next->state = TASK_RUNNING;
    task_switch_to(next);
    *regs = next->saved_regs;
    return 1;
}

#define EXEC_ARG_MAX 32
#define EXEC_ARG_BYTES 1024

typedef struct {
    unsigned count;
    char *values[EXEC_ARG_MAX + 1];
    char strings[EXEC_ARG_BYTES];
} exec_args_t;

/* Copy everything before switching away from the caller's address space. */
static int task_copy_args(exec_args_t *args, const char *const *argv) {
    unsigned used = 0;
    args->count = 0;
    if (!argv) return 0;
    for (unsigned i = 0; i <= EXEC_ARG_MAX; i++) {
        const char *value;
        uintptr_t slot = (uintptr_t)argv + i * sizeof(value);
        if (slot < (uintptr_t)argv ||
            copy_from_user(&value, (const void *)slot, sizeof(value)) < 0)
            return -K_EFAULT;
        if (!value) return 0;
        if (i == EXEC_ARG_MAX) return -K_E2BIG;
        args->values[i] = args->strings + used;
        for (unsigned j = 0;; j++) {
            char c;
            if (used == EXEC_ARG_BYTES) return -K_E2BIG;
            uintptr_t address = (uintptr_t)value + j;
            if (address < (uintptr_t)value ||
                copy_from_user(&c, (const void *)address, 1) < 0)
                return -K_EFAULT;
            args->strings[used++] = c;
            if (!c) break;
        }
        args->count++;
    }
    return -K_E2BIG;
}

static int task_prepare_user_stack(task_t *task, const char *path,
                                   const exec_args_t *args) {
    /* Reject an ELF image overlapping the stack or its guard page. */
    for (uintptr_t page = USER_STACK_TOP - USER_STACK_BYTES - PAGE_SIZE;
         page < USER_STACK_TOP; page += PAGE_SIZE) {
        if (vmm_translate(page)) return -1;
    }
    for (uintptr_t stack_page = USER_STACK_TOP - USER_STACK_BYTES;
         stack_page < USER_STACK_TOP; stack_page += PAGE_SIZE) {
        uint32_t stack_phys = pmm_alloc_frame();
        if (!stack_phys) return -1;
        if (vmm_map_page(stack_page, stack_phys, VMM_PRESENT | VMM_RW | VMM_USER) < 0) {
            pmm_free_frame(stack_phys);
            return -1;
        }
        kmemset((void *)stack_page, 0, PAGE_SIZE);
    }
    unsigned argc = args ? args->count : 1;
    uint32_t pointers[EXEC_ARG_MAX];
    uintptr_t sp = USER_STACK_TOP;
    for (unsigned i = 0; i < argc; i++) {
        const char *value = args ? args->values[i] : path;
        size_t length = kstrlen(value) + 1;
        sp -= length;
        kmemcpy((void *)sp, value, length);
        pointers[i] = sp;
    }
    /* Entry ABI: argc, argv pointers, NULL; _start aligns before calling C. */
    sp = (sp - (argc + 2) * sizeof(uint32_t)) & ~15U;
    uint32_t *words = (uint32_t *)sp;
    words[0] = argc;
    for (unsigned i = 0; i < argc; i++) words[i + 1] = pointers[i];
    words[argc + 1] = 0;
    task->user_stack_top = sp;
    return 0;
}

static int task_load_image_into_space(task_t *task, const char *path, page_directory_t *pd,
                                      const exec_args_t *args) {
    elf_load_result_t res;
    char image_path[sizeof(task->image_path)];
    page_directory_t *old_pd;

    if (!task || !path || !pd) {
        return -1;
    }

    if (task_resolve_path(path, image_path, sizeof(image_path)) < 0) return -1;

    task_preemption_disable();
    if (elf_load_from_file_into_address_space(image_path, pd, &res) < 0) {
        task_preemption_enable();
        return -1;
    }

    old_pd = vmm_current_address_space();
    vmm_switch_address_space(pd);
    if (task_prepare_user_stack(task, image_path, args) < 0) {
        vmm_switch_address_space(old_pd);
        task_preemption_enable();
        return -1;
    }
    vmm_switch_address_space(old_pd);
    task_preemption_enable();

    task->entry = res.entry;
    task->heap_start = res.heap_start;
    task->heap_brk = res.heap_start;
    task->heap_limit = USER_HEAP_LIMIT;
    task->state = TASK_READY;
    task->exit_status = 0;
    task_clear_wait_state(task);
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

    if (task_load_image_into_space(task, path, pd, NULL) < 0) {
        vmm_destroy_address_space(pd);
        task_free_kernel_stack(task);
        return -1;
    }

    task->address_space = pd;
    return 0;
}

void tasking_init(void) {
    fpu_init();
    kmemset(task_table, 0, sizeof(task_table));
    fpu_reset(&task_table[0]);
    task_table[0].pid = next_pid++;
    task_table[0].state = TASK_UNUSED;
    task_table[0].user_stack_top = USER_STACK_TOP;
    fd_table_init(&task_table[0]);
    kstrcpy(task_table[0].cwd, "/");
    current_task = &task_table[0];
    preempt_disable_depth = 0;
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

int task_exec_current(const char *path, const char *const *argv, interrupt_frame_t *regs) {
    page_directory_t *old_pd;
    page_directory_t *new_pd;

    if (!current_task || !regs) {
        return -1;
    }

    exec_args_t *args = kmalloc(sizeof(*args));
    if (!args) return -K_ENOMEM;
    int result = task_copy_args(args, argv);
    if (result < 0) { kfree(args); return result; }

    old_pd = current_task->address_space;
    new_pd = vmm_create_address_space();
    if (!new_pd) {
        kfree(args);
        return -K_ENOMEM;
    }

    result = task_load_image_into_space(current_task, path, new_pd, argv ? args : NULL);
    kfree(args);
    if (result < 0) {
        vmm_destroy_address_space(new_pd);
        return -K_ENOENT;
    }

    fpu_reset(current_task);
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

/* Publish the child and cancellation state atomically with respect to Ctrl+C. */
static void task_publish_child(task_t *child, task_t *parent) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    child->interrupt_pending = parent->job_cancelled || parent->interrupt_pending;
    child->state = TASK_READY;
    __asm__ volatile ("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
}

int task_spawn_current(const char *path, interrupt_frame_t *regs) {
    task_t *parent = current_task;
    task_t *child;
    char image_path[TASK_IMAGE_PATH_BYTES];

    if (!parent || !regs || !path) {
        return -1;
    }

    task_copy_path(image_path, sizeof(image_path), path);

    if (parent->job_cancelled || parent->interrupt_pending) return -K_EAGAIN;
    child = task_alloc();
    if (!child) {
        return -1;
    }

    parent->saved_regs = *regs;
    parent->saved_regs.eax = child->pid;

    kmemcpy(child->cwd, parent->cwd, sizeof(child->cwd));
    child->parent_pid = parent->pid;
    child->job_id = parent->child_job_id ? parent->child_job_id : parent->job_id;

    if (task_create_and_load_image(child, image_path) < 0) {
        child->state = TASK_UNUSED;
        return -1;
    }

    child->saved_regs = *regs;
    child->saved_regs.eip = child->entry;
    child->saved_regs.useresp = child->user_stack_top;
    child->saved_regs.eax = 0;
    task_publish_child(child, parent);
    regs->eax = child->pid;

    kprintf("task: pid=%u spawned child pid=%u image=%s entry=0x%x\n",
            parent->pid,
            child->pid,
            child->image_path,
            child->entry);

    return 0;
}

int task_fork_current(interrupt_frame_t *regs) {
    task_t *parent = current_task;
    task_t *child;

    if (!parent || !regs || !parent->address_space) return -K_EINVAL;
    if (parent->job_cancelled || parent->interrupt_pending) return -K_EAGAIN;
    child = task_alloc();
    if (!child) return -K_EAGAIN;
    if (task_alloc_kernel_stack(child) < 0) goto fail;

    child->address_space = vmm_clone_address_space(parent->address_space);
    if (!child->address_space) goto fail;
    kmemcpy(child->cwd, parent->cwd, sizeof(child->cwd));
    child->parent_pid = parent->pid;
    child->job_id = parent->child_job_id ? parent->child_job_id : parent->job_id;
    child->entry = parent->entry;
    child->user_stack_top = parent->user_stack_top;
    child->heap_start = parent->heap_start;
    child->heap_brk = parent->heap_brk;
    child->heap_limit = parent->heap_limit;
    child->exit_status = 0;
    task_copy_path(child->image_path, sizeof(child->image_path), parent->image_path);
    if (fd_table_clone(child, parent) < 0) goto fail;

    __asm__ volatile ("fxsave %0" : "=m"(parent->fpu_state));
    kmemcpy(child->fpu_state, parent->fpu_state, sizeof(parent->fpu_state));
    child->saved_regs = *regs;
    child->saved_regs.eax = 0;
    task_publish_child(child, parent);
    regs->eax = child->pid;
    return (int)child->pid;

fail:
    fd_table_close_all(child);
    if (child->address_space) vmm_destroy_address_space(child->address_space);
    task_free_kernel_stack(child);
    kmemset(child, 0, sizeof(*child));
    return -K_ENOMEM;
}

void task_yield_current(interrupt_frame_t *regs) {
    int switched = task_schedule(regs);
    if (switched <= 0 && regs) {
        regs->eax = 0;
    }
}

void task_preempt_current(interrupt_frame_t *regs) {
    if (!regs || (regs->cs & 3U) != 3U || preempt_disable_depth != 0 ||
        !current_task || current_task->state != TASK_RUNNING) {
        return;
    }

    task_schedule(regs);
}

int task_block_current_for_keyboard(interrupt_frame_t *regs, char *buf, uint32_t count) {
    if (!current_task || !regs || !buf || count == 0) {
        return -1;
    }

    current_task->saved_regs = *regs;
    current_task->saved_regs.eax = 0;
    current_task->keyboard_read_buf = (uint32_t)buf;
    current_task->keyboard_read_count = count;
    current_task->waiting_for_keyboard = 1;
    current_task->state = TASK_BLOCKED;

    return task_schedule(regs) < 0 ? -1 : 0;
}

int task_complete_keyboard_read(char c) {
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        task_t *task = &task_table[i];

        if (task->state == TASK_BLOCKED && task->waiting_for_keyboard &&
            task_terminal_can_read(task)) {
            page_directory_t *old_pd = vmm_current_address_space();

            vmm_switch_address_space(task->address_space);
            if (c == 4) {
                task->saved_regs.eax = 0;
            } else if (copy_to_user((void *)task->keyboard_read_buf, &c, 1) < 0) {
                task->saved_regs.eax = (uint32_t)-K_EFAULT;
            } else {
                task->saved_regs.eax = 1;
            }
            vmm_switch_address_space(old_pd);

            task->keyboard_read_buf = 0;
            task->keyboard_read_count = 0;
            task->waiting_for_keyboard = 0;
            task->state = TASK_READY;
            return 1;
        }
    }

    return 0;
}

int task_block_current_for_pipe(interrupt_frame_t *regs, void *pipe) {
    if (!current_task || !regs || !pipe) return -1;
    current_task->saved_regs = *regs;
    /* int $0x80 is two bytes; retry the syscall after the pipe changes. */
    current_task->saved_regs.eip -= 2;
    current_task->waiting_pipe = pipe;
    current_task->state = TASK_BLOCKED;
    return task_schedule(regs) < 0 ? -1 : 0;
}

void task_wake_pipe(void *pipe) {
    if (!pipe) return;
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (task_table[i].state == TASK_BLOCKED && task_table[i].waiting_pipe == pipe) {
            task_table[i].waiting_pipe = NULL;
            task_table[i].state = TASK_READY;
        }
    }
}

void task_sleep_current(uint32_t sleep_ticks, interrupt_frame_t *regs) {
    if (!current_task || !regs) {
        return;
    }

    if (sleep_ticks == 0) {
        regs->eax = 0;
        return;
    }

    current_task->saved_regs = *regs;
    current_task->saved_regs.eax = 0;
    current_task->wake_tick = timer_ticks() + sleep_ticks;
    current_task->state = TASK_SLEEPING;

    if (task_schedule(regs) < 0) {
        current_task->wake_tick = 0;
        current_task->state = TASK_RUNNING;
        regs->eax = (uint32_t)-K_EAGAIN;
    }
}

void task_wake_sleepers(uint32_t now) {
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        task_t *task = &task_table[i];

        if (task->state == TASK_SLEEPING &&
            (int32_t)(now - task->wake_tick) >= 0) {
            task->wake_tick = 0;
            task->state = TASK_READY;
        }
    }
}

void task_wait_current(uint32_t pid, interrupt_frame_t *regs) {
    task_t *child;
    int status;

    if (!current_task || !regs || pid == 0) {
        if (regs) {
            regs->eax = (uint32_t)-K_ECHILD;
        }
        return;
    }

    child = task_find_by_pid(pid);
    if (!child || child->parent_pid != current_task->pid) {
        regs->eax = (uint32_t)-K_ECHILD;
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
        regs->eax = (uint32_t)-K_EAGAIN;
    }
}

void task_reap_exited_children(void) {
    task_t *parent = current_task;

    if (!parent) {
        return;
    }

    /* Detached orphans must also be reclaimed while init waits for a nested shell. */
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        task_t *child = &task_table[i];

        if (child == parent ||
            child->state != TASK_EXITED ||
            (child->parent_pid != parent->pid && !child->detached) ||
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
    if (terminal_owner == exiting->pid) {
        terminal_owner = exiting->terminal_parent;
        exiting->terminal_parent = 0;
    }
    exiting->child_job_id = 0;
    exiting->job_cancelled = 0;
    fd_table_close_all(exiting);
    exiting->state = TASK_EXITED;
    exiting->exit_status = status;
    exiting->waiting_for_pid = 0;
    exiting->wake_tick = 0;
    exiting->keyboard_read_buf = 0;
    exiting->keyboard_read_count = 0;
    exiting->waiting_for_keyboard = 0;
    exiting->waiting_pipe = NULL;

    /* Keep descendants owned by init so their task slots can be reclaimed. */
    if (exiting != &task_table[0]) {
        for (uint32_t i = 1; i < TASK_MAX; i++) {
            if (task_table[i].state != TASK_UNUSED &&
                task_table[i].parent_pid == exiting->pid) {
                task_table[i].parent_pid = task_table[0].pid;
                task_table[i].detached = 1;
                if (task_table[i].state == TASK_EXITED) {
                    task_table[i].wait_status_delivered = 1;
                }
            }
        }
    }

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

        if (exiting->detached) {
            exiting->wait_status_delivered = 1;
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

    if (task_create_and_load_image(exiting, "/bin/shell") < 0 &&
        task_create_and_load_image(exiting, "/shell") < 0) {
        kprintf("task: failed to restart /bin/shell\n");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    fpu_reset(exiting);
    fd_table_init(exiting);
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

    user_stack_base = USER_STACK_TOP - USER_STACK_BYTES;
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
