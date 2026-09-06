#include "kernel/multiboot.h"
#include "kernel/pmm.h"
#include <stdint.h>
#include <libk/stdio.h>
#include <libk/string.h>

#define ADDR_OFFSET 0xC0000000U

// Physical memory covered by the kernel's bootstrap direct map.
#define PAGE_SIZE       0x1000U                          // 4 KiB
#define MAX_PHYS_MEM    (64U * 1024U * 1024U)
#define MAX_FRAMES      (MAX_PHYS_MEM / PAGE_SIZE)
#define BITMAP_BYTES    (MAX_FRAMES / 8)

// 把 physaddr 向下对齐到 PAGE_SIZE 的边界
#define ALIGN_DOWN(a, sz)   ((a) & ~((sz) - 1))

static uint8_t pmm_bitmap[BITMAP_BYTES];

// 位图基本操作
static inline void bitmap_set(uint32_t bit)   { pmm_bitmap[bit >> 3] |=  (1 << (bit & 7)); }
static inline void bitmap_clear(uint32_t bit) { pmm_bitmap[bit >> 3] &= ~(1 << (bit & 7)); }
static inline int  bitmap_test(uint32_t bit)  { return (pmm_bitmap[bit >> 3] >> (bit & 7)) & 1; }



void pmm_init(multiboot_info_t* mbd, uint32_t magic)
{

    // 1) 全图置 1
    kmemset(pmm_bitmap, 0xFF, BITMAP_BYTES);

    /* Make sure the magic number matches for memory mapping*/
    if(magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kprintf("invalid magic number! \n");
    }

    // Convert the physical pointer to its high-half virtual address:
    mbd = (multiboot_info_t*)((uintptr_t)mbd + ADDR_OFFSET);


    /* Check bit 6 to see if we have a valid memory map */
    if(!(mbd->flags >> 6 & 0x1)) {
        kprintf("invalid memory map given by GRUB bootloader \n");
    }

    // 2) 遍历 multiboot memory map，把 type=1 的页清空为“空闲”
    uint32_t entries = mbd->mmap_length / sizeof(multiboot_memory_map_t);
    multiboot_memory_map_t* map =
        (multiboot_memory_map_t*)(uintptr_t)(mbd->mmap_addr + ADDR_OFFSET);

    for(uint32_t i = 0; i < entries; i++) {
        uint32_t base = map->addr_low;
        uint32_t len  = map->len_low;

        if(map->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint32_t start = (base + PAGE_SIZE - 1) / PAGE_SIZE;
            uint64_t region_end = (uint64_t)base + len;
            uint32_t end = region_end > MAX_PHYS_MEM
                         ? MAX_FRAMES
                         : (uint32_t)(region_end / PAGE_SIZE);
            if (start > MAX_FRAMES) {
                start = MAX_FRAMES;
            }
            for(uint32_t f = start; f < end; f++) {
                bitmap_clear(f);
            }
        }
        // advance to next entry: size 字段后面紧跟下一条
        map = (multiboot_memory_map_t*)
              ((uintptr_t)map + map->size + sizeof(map->size));
    }

    // 3) 把内核自身占用的页又标回“占用”
    extern uint8_t _kernel_start, _kernel_end;
    uint32_t phys_start = (uint32_t)&_kernel_start;
    uint32_t phys_end   = (uint32_t)&_kernel_end   - ADDR_OFFSET;
    uint32_t kstart = phys_start / PAGE_SIZE;
    uint32_t kend   = (phys_end   + PAGE_SIZE - 1) / PAGE_SIZE;
    // kprintf("Kernel phys start 0x%x, phys end 0x%x\n", phys_start, phys_end);
    for(uint32_t f = kstart; f < kend; f++) {
        bitmap_set(f);
    }


    uint32_t zero_addr = pmm_alloc_frame();
    if(zero_addr){
        kprintf("Physical memory manager initilizatio failed \n");
        asm volatile ("1: jmp 1b");
    }
    
}

// 按页分配：先扫描 last_alloc 开始的空闲页
static uint32_t last_alloc = 0;
uint32_t pmm_alloc_frame(void)
{
    for(uint32_t i = last_alloc; i < MAX_FRAMES; i++) {
        if(!bitmap_test(i)) {
            bitmap_set(i);
            last_alloc = i + 1;
            return i * PAGE_SIZE;
        }
    }
    for(uint32_t i = 0; i < last_alloc; i++) {
        if(!bitmap_test(i)) {
            bitmap_set(i);
            last_alloc = i + 1;
            return i * PAGE_SIZE;
        }
    }
    return 0;  // 没有空闲页了
}

// 释放物理页
void pmm_free_frame(uint32_t physaddr)
{
    uint32_t frame = physaddr / PAGE_SIZE;
    bitmap_clear(frame);
    if(frame < last_alloc) last_alloc = frame;
}

// void pmm_test_frame(uint32_t physaddr)
// {
//     // 1) 对齐到页起始
//     uint32_t page_addr = ALIGN_DOWN(physaddr, PAGE_SIZE);
//     // 2) 计算对应的 frame 索引
//     uint32_t frame = page_addr / PAGE_SIZE;

//     // 3) 越界检查
//     if (frame >= MAX_FRAMES) {
//         kprintf("pmm_test_frame: address 0x%x out of managed range\n", physaddr);
//         return;
//     }

//     // 4) 查询位图
//     if (bitmap_test(frame)) {
//         kprintf("Frame at phys 0x%x [%u] is **allocated**\n",
//                page_addr, frame);
//     } else {
//         kprintf("Frame at phys 0x%x [%u] is **available**\n",
//                page_addr, frame);
//     }
// }
