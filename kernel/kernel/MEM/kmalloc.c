#include <stddef.h>
#include <stdint.h>
#include <kernel/kha.h>
#include <kernel/kmalloc.h>
#include <kernel/task.h>

#define KMALLOC_ALIGN ((size_t)8)
#define PAGE_SIZE ((size_t)4096)
#define VMM_PRESENT 1U
#define VMM_RW 2U

typedef struct kmem_block {
    size_t size; /* Includes this header. */
    struct kmem_block *next;
} kmem_block_t;

static kmem_block_t *free_list;

/* Keep free ranges ordered and merge only adjacent virtual ranges.
 * Page allocations used by other kernel subsystems may separate heap chunks. */
static void insert_free(kmem_block_t *block) {
    kmem_block_t *previous = NULL, *next = free_list;
    while (next && (uintptr_t)next < (uintptr_t)block) {
        previous = next;
        next = next->next;
    }
    block->next = next;
    if (next && (uintptr_t)block + block->size == (uintptr_t)next) {
        block->size += next->size;
        block->next = next->next;
    }
    if (previous && (uintptr_t)previous + previous->size == (uintptr_t)block) {
        previous->size += block->size;
        previous->next = block->next;
    } else if (previous) previous->next = block;
    else free_list = block;
}

void *kmalloc(size_t size) {
    if (!size || size > SIZE_MAX - sizeof(kmem_block_t) - (PAGE_SIZE - 1)) return NULL;
    size_t need = ((size + KMALLOC_ALIGN - 1) & ~(KMALLOC_ALIGN - 1)) + sizeof(kmem_block_t);
    task_preemption_disable();
    for (;;) {
        kmem_block_t **link = &free_list;
        while (*link && (*link)->size < need) link = &(*link)->next;
        kmem_block_t *block = *link;
        if (block) {
            size_t remaining = block->size - need;
            if (remaining >= sizeof(kmem_block_t) + KMALLOC_ALIGN) {
                kmem_block_t *rest = (void *)((uint8_t *)block + need);
                rest->size = remaining;
                rest->next = block->next;
                *link = rest;
                block->size = need;
            } else *link = block->next;
            block->next = NULL;
            task_preemption_enable();
            return block + 1;
        }
        size_t total = (need + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        block = vmm_alloc_pages(total / PAGE_SIZE, VMM_PRESENT | VMM_RW);
        if (!block) { task_preemption_enable(); return NULL; }
        block->size = total;
        insert_free(block);
    }
}

void kfree(void *ptr) {
    if (!ptr) return;
    task_preemption_disable();
    insert_free((kmem_block_t *)ptr - 1);
    task_preemption_enable();
}
