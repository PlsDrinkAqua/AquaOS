#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define MALLOC_ALIGN 8U
#define CHUNK_SIZE   4096U

typedef struct malloc_block {
    size_t size;
    int free;
    struct malloc_block *next;
} __attribute__((aligned(MALLOC_ALIGN))) malloc_block_t;

static malloc_block_t *heap_head;
static malloc_block_t *heap_tail;

static size_t align_up(size_t value, size_t align) {
    return (value + align - 1) & ~(align - 1);
}

static malloc_block_t *find_free_block(size_t size) {
    malloc_block_t *cur = heap_head;

    while (cur) {
        if (cur->free && cur->size >= size) {
            return cur;
        }
        cur = cur->next;
    }

    return NULL;
}

static void split_block(malloc_block_t *block, size_t size) {
    size_t remaining;
    malloc_block_t *split;

    if (!block || block->size < size + sizeof(malloc_block_t) + MALLOC_ALIGN) {
        return;
    }

    remaining = block->size - size - sizeof(malloc_block_t);
    split = (malloc_block_t *)((uint8_t *)(block + 1) + size);
    split->size = remaining;
    split->free = 1;
    split->next = block->next;

    block->size = size;
    block->next = split;

    if (heap_tail == block) {
        heap_tail = split;
    }
}

static malloc_block_t *request_block(size_t size) {
    size_t total = align_up(size + sizeof(malloc_block_t), CHUNK_SIZE);
    void *current = sbrk(0);
    if (current == (void *)-1) return NULL;
    size_t padding = (0U - (uintptr_t)current) & (MALLOC_ALIGN - 1);
    void *base = sbrk((intptr_t)(total + padding));
    if (base == (void *)-1) return NULL;
    malloc_block_t *block = (void *)((uint8_t *)base + padding);

    block->size = total - sizeof(malloc_block_t);
    block->free = 0;
    block->next = NULL;

    if (heap_tail) {
        heap_tail->next = block;
    } else {
        heap_head = block;
    }
    heap_tail = block;

    split_block(block, size);
    return block;
}

void *malloc(size_t size) {
    malloc_block_t *block;

    if (size > (size_t)INTPTR_MAX - sizeof(malloc_block_t) - (CHUNK_SIZE - 1)) {
        errno = ENOMEM;
        return NULL;
    }

    if (size == 0) {
        return NULL;
    }

    size = align_up(size, MALLOC_ALIGN);
    block = find_free_block(size);
    if (!block) {
        block = request_block(size);
        if (!block) {
            return NULL;
        }
    }

    block->free = 0;
    split_block(block, size);
    return block + 1;
}

static void coalesce_free_blocks(void) {
    malloc_block_t *cur = heap_head;

    while (cur && cur->next) {
        if (cur->free && cur->next->free &&
            (uint8_t *)(cur + 1) + cur->size == (uint8_t *)cur->next) {
            cur->size += sizeof(malloc_block_t) + cur->next->size;
            cur->next = cur->next->next;
            if (!cur->next) {
                heap_tail = cur;
            }
        } else {
            cur = cur->next;
        }
    }
}

void free(void *ptr) {
    malloc_block_t *block;

    if (!ptr) {
        return;
    }

    block = ((malloc_block_t *)ptr) - 1;
    block->free = 1;
    coalesce_free_blocks();
}

void *calloc(size_t nmemb, size_t size) {
    size_t total;
    uint8_t *ptr;

    if (nmemb != 0 && size > ((size_t)-1) / nmemb) {
        errno = ENOMEM;
        return NULL;
    }

    total = nmemb * size;
    ptr = malloc(total);
    if (!ptr) {
        return NULL;
    }

    for (size_t i = 0; i < total; i++) {
        ptr[i] = 0;
    }

    return ptr;
}

/* A failed growth leaves the original allocation owned by the caller. */
void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (!size) { free(ptr); return NULL; }
    if (size > (size_t)INTPTR_MAX - sizeof(malloc_block_t) - (CHUNK_SIZE - 1)) {
        errno = ENOMEM;
        return NULL;
    }
    malloc_block_t *block = (malloc_block_t *)ptr - 1;
    size_t aligned = align_up(size, MALLOC_ALIGN);
    if (aligned <= block->size) {
        split_block(block, aligned);
        coalesce_free_blocks();
        return ptr;
    }
    malloc_block_t *next = block->next;
    if (next && next->free && (uint8_t *)(block + 1) + block->size == (uint8_t *)next &&
        block->size + sizeof(*next) + next->size >= aligned) {
        block->size += sizeof(*next) + next->size;
        block->next = next->next;
        if (heap_tail == next) heap_tail = block;
        split_block(block, aligned);
        return ptr;
    }
    void *replacement = malloc(size);
    if (!replacement) return NULL;
    for (size_t i = 0; i < block->size; i++) ((uint8_t *)replacement)[i] = ((uint8_t *)ptr)[i];
    free(ptr);
    return replacement;
}
