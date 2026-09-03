#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "kernel/vmm.h"
#include "kernel/pmm.h"
#include "kernel/user_heap.h"

#define PAGE_SIZE       0x1000U
#define ALIGN_UP(x, a)  (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x,a) ((x) & ~((a) - 1))

#define VMM_PRESENT  (1 << 0)
#define VMM_RW       (1 << 1)
#define VMM_USER     (1 << 2)

#define USER_HEAP_START  0x40000000U
#define USER_HEAP_LIMIT  0x80000000U

static uintptr_t user_heap_start = USER_HEAP_START;
static uintptr_t user_brk        = USER_HEAP_START;
static uintptr_t user_heap_limit = USER_HEAP_LIMIT;

void user_heap_init(void) {
    user_heap_start = USER_HEAP_START;
    user_brk        = USER_HEAP_START;
    user_heap_limit = USER_HEAP_LIMIT;
}

uintptr_t user_heap_start_vaddr(void) {
    return user_heap_start;
}

uintptr_t user_heap_brk_vaddr(void) {
    return user_brk;
}

uintptr_t user_heap_limit_vaddr(void) {
    return user_heap_limit;
}

void *sys_sbrk(intptr_t increment) {
    uintptr_t old_brk = user_brk;
    uintptr_t new_brk;

    // sbrk(0): just return current break
    if (increment == 0) {
        return (void *)old_brk;
    }

    // 防止整数溢出/下溢
    if (increment > 0) {
        if (old_brk > user_heap_limit - (uintptr_t)increment) {
            return (void *)-1;
        }
        new_brk = old_brk + (uintptr_t)increment;
    } else {
        uintptr_t dec = (uintptr_t)(-increment);
        if (old_brk < user_heap_start + dec) {
            return (void *)-1;
        }
        new_brk = old_brk - dec;
    }

    // 再做一次边界检查
    if (new_brk < user_heap_start || new_brk > user_heap_limit) {
        return (void *)-1;
    }

    if (increment > 0) {
        // old_brk -> new_brk 之间如果跨了页边界，需要补映射新页
        //
        // 例如：
        // old_brk = 0x40000010
        // new_brk = 0x40002020
        //
        // 需要映射：
        // 0x40001000, 0x40002000 这些新进入范围的页
        uintptr_t map_start = ALIGN_UP(old_brk, PAGE_SIZE);
        uintptr_t map_end   = ALIGN_UP(new_brk, PAGE_SIZE);

        for (uintptr_t va = map_start; va < map_end; va += PAGE_SIZE) {
            uint32_t phys = pmm_alloc_frame();
            if (!phys) {
                // rollback 已映射的页
                for (uintptr_t rva = map_start; rva < va; rva += PAGE_SIZE) {
                    vmm_unmap_page(rva, true);
                }
                return (void *)-1;
            }

            if (vmm_map_page(va, phys, VMM_PRESENT | VMM_RW | VMM_USER) < 0) {
                pmm_free_frame(phys);

                // rollback 已映射的页
                for (uintptr_t rva = map_start; rva < va; rva += PAGE_SIZE) {
                    vmm_unmap_page(rva, true);
                }
                return (void *)-1;
            }
        }
    } else {
        // shrink heap
        //
        // old_brk = 0x40002020
        // new_brk = 0x40000010
        //
        // 需要释放已经不再覆盖的整页：
        // [ALIGN_UP(new_brk), ALIGN_UP(old_brk))
        uintptr_t unmap_start = ALIGN_UP(new_brk, PAGE_SIZE);
        uintptr_t unmap_end   = ALIGN_UP(old_brk, PAGE_SIZE);

        for (uintptr_t va = unmap_start; va < unmap_end; va += PAGE_SIZE) {
            vmm_unmap_page(va, true);
        }
    }

    user_brk = new_brk;
    return (void *)old_brk;
}
