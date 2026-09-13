/* Exercise the real allocators with bounded, deterministic backing storage. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

void *kmalloc(size_t);
void kfree(void *);
void *zenos_malloc(size_t);
void zenos_free(void *);
void *zenos_calloc(size_t, size_t);
static _Alignas(4096) unsigned char kernel_arena[65536], user_arena[65536];
static size_t kernel_used, user_used;
static int lock_depth;
int zenos_errno;
void task_preemption_disable(void) { lock_depth++; }
void task_preemption_enable(void) { assert(lock_depth > 0); lock_depth--; }
void *vmm_alloc_pages(size_t n, uint32_t flags) {
    (void)flags;
    if (!n || n > (sizeof(kernel_arena) - kernel_used) / 4096) return NULL;
    void *p = kernel_arena + kernel_used;
    kernel_used += n * 4096;
    return p;
}
void *zenos_sbrk(intptr_t n) {
    if (n < 0 || (uintptr_t)n > sizeof(user_arena) - user_used) return (void *)-1;
    void *p = user_arena + user_used;
    user_used += n;
    return p;
}
static void kernel_checks(void) {
    assert(kmalloc(SIZE_MAX) == NULL);
    assert(kmalloc(SIZE_MAX - 7) == NULL);
    assert(kmalloc((size_t)UINT32_MAX + 1) == NULL);
    assert(kernel_used == 0);
    void *a = kmalloc(1000), *b = kmalloc(1000), *c = kmalloc(1000);
    assert(a && b && c);
    memset(a, 1, 1000); memset(b, 2, 1000); memset(c, 3, 1000);
    size_t before = kernel_used;
    kfree(b); kfree(a);
    void *merged = kmalloc(1900);
    assert(merged && kernel_used == before);
    for (unsigned i = 0; i < 1000; i++) assert(((unsigned char *)c)[i] == 3);
    kfree(c); kfree(merged);
    void *whole = kmalloc(4000);
    assert(whole && kernel_used == before);
    kfree(whole);
    for (int pass = 0; pass < 50; pass++) {
        void *parts[32];
        for (int i = 0; i < 32; i++) { parts[i] = kmalloc(64); assert(parts[i]); }
        for (int i = 0; i < 32; i += 2) kfree(parts[i]);
        for (int i = 1; i < 32; i += 2) kfree(parts[i]);
        assert(kernel_used == before);
    }
    void *held[32]; int count = 0;
    while (count < 32 && (held[count] = kmalloc(4000))) count++;
    assert(count < 32);
    for (int i = 0; i < count; i++) kfree(held[i]);
    whole = kmalloc(60000); assert(whole); kfree(whole);
    assert(lock_depth == 0);
}
static void user_checks(void) {
    assert(zenos_malloc(SIZE_MAX) == NULL);
    assert(zenos_malloc(SIZE_MAX - 7) == NULL);
    assert(zenos_calloc(SIZE_MAX, 2) == NULL);
    assert(user_used == 0);
    void *a = zenos_malloc(3000); assert(a); zenos_free(a);
    /* An application may also grow the break: never merge across its data. */
    unsigned char *gap = zenos_sbrk(4096); memset(gap, 0x5a, 4096);
    void *b = zenos_malloc(6000); assert(b); zenos_free(b);
    void *c = zenos_malloc(9000); assert(c); memset(c, 0xa5, 9000);
    for (int i = 0; i < 4096; i++) assert(gap[i] == 0x5a);
    zenos_free(c);
}
int main(int argc, char **argv) {
    (void)argv;
    if (argc > 1) user_checks(); else kernel_checks();
    puts("allocator checks passed");
    return 0;
}
