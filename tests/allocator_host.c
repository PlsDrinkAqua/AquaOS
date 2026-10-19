/* Exercise the real allocators with bounded, deterministic backing storage. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

void *kmalloc(size_t);
void kfree(void *);
void *aquaos_malloc(size_t);
void *aquaos_realloc(void *, size_t);
void aquaos_free(void *);
void *aquaos_calloc(size_t, size_t);
static _Alignas(4096) unsigned char kernel_arena[65536], user_arena[65536];
static size_t kernel_used, user_used;
static int lock_depth;
int aquaos_errno;
void task_preemption_disable(void) { lock_depth++; }
void task_preemption_enable(void) { assert(lock_depth > 0); lock_depth--; }
void *vmm_alloc_pages(size_t n, uint32_t flags) {
    (void)flags;
    if (!n || n > (sizeof(kernel_arena) - kernel_used) / 4096) return NULL;
    void *p = kernel_arena + kernel_used;
    kernel_used += n * 4096;
    return p;
}
void *aquaos_sbrk(intptr_t n) {
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
    assert(aquaos_malloc(SIZE_MAX) == NULL);
    assert(aquaos_malloc(SIZE_MAX - 7) == NULL);
    assert(aquaos_calloc(SIZE_MAX, 2) == NULL);
    assert(user_used == 0);
    void *a = aquaos_malloc(3000); assert(a); aquaos_free(a);
    /* An application may also grow the break: never merge across its data. */
    unsigned char *gap = aquaos_sbrk(4096); memset(gap, 0x5a, 4096);
    void *b = aquaos_malloc(6000); assert(b); aquaos_free(b);
    void *c = aquaos_malloc(9000); assert(c); memset(c, 0xa5, 9000);
    for (int i = 0; i < 4096; i++) assert(gap[i] == 0x5a);
    aquaos_free(c);
    unsigned char *p = aquaos_malloc(64);
    assert(p); memset(p, 0x3a, 64);
    void *guard = aquaos_malloc(512); assert(guard);
    void *failed = aquaos_realloc(p, 100000);
    assert(!failed);
    for (int i = 0; i < 64; i++) assert(p[i] == 0x3a);
    unsigned char *moved = aquaos_realloc(p, 2000); assert(moved);
    for (int i = 0; i < 64; i++) assert(moved[i] == 0x3a);
    assert(aquaos_realloc(moved, 8) == moved);
    assert(aquaos_realloc(moved, SIZE_MAX) == NULL && moved[0] == 0x3a);
    assert(aquaos_realloc(moved, 0) == NULL);
    aquaos_free(guard);
    p = aquaos_malloc(64); guard = aquaos_malloc(512); assert(p && guard);
    aquaos_free(guard); assert(aquaos_realloc(p, 128) == p); aquaos_free(p);
}
int main(int argc, char **argv) {
    (void)argv;
    if (argc > 1) user_checks(); else kernel_checks();
    puts("allocator checks passed");
    return 0;
}
