#include <stdint.h>
#include <libk/string.h>
#include <libk/stdio.h>
#include <stdbool.h>
#include "kernel/vmm.h"       // alloc_frame/free_frame，如果今后需要动态分配页表
#include "kernel/paging.h"
#include "kernel/pmm.h"
#include "kernel/kha.h"


#define KERNEL_HEAP_START  0xD0000000UL
#define PAGE_SIZE       0x1000U                          // 4 KiB

#define VMM_PRESENT  (1<<0)
#define VMM_RW       (1<<1)
#define VMM_USER     (1<<2)

// 当前的 heap break（下一个可用虚拟地址）
static uintptr_t heap_brk;

// Keep heap mappings separate from the physical direct-map window.
void vmm_heap_init(void) {
    heap_brk = KERNEL_HEAP_START;
}

// 2) 按页分配：连续 npages 页，每页物理分配 + 虚拟映射
//    flags 对应 VMM_PRESENT|VMM_RW|VMM_USER 等
void *vmm_alloc_pages(size_t npages, uint32_t flags) {
    // 对齐 heap_brk 到页边界
    uintptr_t va = (heap_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uintptr_t base = va;
    if (!npages || va < heap_brk || npages > (UINTPTR_MAX - va) / PAGE_SIZE) return NULL;

    for (size_t i = 0; i < npages; i++) {
        // 分配物理页
        uint32_t phys = pmm_alloc_frame();
        if (!phys) {
            // 失败：回退已经映射的页
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(base + j * PAGE_SIZE, true);
            }
            return NULL;
        }
        // 映射到虚拟地址
        if (vmm_map_page(va + i * PAGE_SIZE, phys, flags) < 0) {
            // 失败：释放刚分配的物理页，并回退已映射页
            pmm_free_frame(phys);
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(base + j * PAGE_SIZE, true);
            }
            return NULL;
        }
    }

    // 成功，推进 heap_brk
    heap_brk = va + npages * PAGE_SIZE;
    return (void*)base;
}

// 3) 释放一块连续的 npages：逐页 unmap + free
void vmm_free_pages(void *ptr, size_t npages) {
    uintptr_t va = (uintptr_t)ptr;
    for (size_t i = 0; i < npages; i++) {
        vmm_unmap_page(va + i * PAGE_SIZE, true);
    }
}

// Test function for vmm_alloc_pages and vmm_free_pages
void vmm_heap_test(void) {
    const size_t npages = 3;

    // Allocate pages
    void *base = vmm_alloc_pages(npages, VMM_PRESENT | VMM_RW);
    if (!base) {
        kprintf("[heap_test] vmm_alloc_pages failed\n");
        return;
    }
    kprintf("[heap_test] Allocated %u pages at virtual address %p\n", npages, base);

    // Write test pattern: each page first byte = page index
    uint8_t *p = (uint8_t*)base;
    for (size_t i = 0; i < npages; i++) {
        p[i * PAGE_SIZE] = (uint8_t)i;
    }
    
    // Read back and verify
    for (size_t i = 0; i < npages; i++) {
        uint8_t val = p[i * PAGE_SIZE];
        if (val != (uint8_t)i) {
            kprintf("[heap_test] Memory mismatch at page %u: expected 0x%x, got 0x%x\n", i, (uint8_t)i, val);
            return;
        }
    }
    kprintf("[heap_test] Read/write test PASS\n");

    // Free pages
    vmm_free_pages(base, npages);
    kprintf("[heap_test] Freed %u pages at virtual address %p\n", npages, base);

    // Verify unmapped: translation should return 0
    uintptr_t va = (uintptr_t)base;
    uint32_t trans = vmm_translate(va);
    if (trans != 0) {
        kprintf("[heap_test] vmm_free_pages failed: translation still returns 0x%x\n", trans);
    } else {
        kprintf("[heap_test] vmm_free_pages PASS: pages unmapped successfully\n");
    }
}
