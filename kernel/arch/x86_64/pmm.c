#include <kernel/pmm.h>
#include <kernel/layout.h>
#include <libk/string.h>
#include <libk/stdio.h>
/* Manage usable firmware RAM within the dedicated physical window. */
#define LIMIT KERNEL_DIRECT_SIZE
#define FRAMES (LIMIT/4096)
static uint8_t bitmap[FRAMES/8];
static unsigned hint;
static unsigned managed_frames;
static void reserve(unsigned n) { bitmap[n/8]|=1U<<(n%8); }
void pmm_init(multiboot_info_t *physical_info,uint32_t magic) {
    multiboot_info_t *info=(void *)(KERNEL_DIRECT_BASE+(uintptr_t)physical_info);
    if(magic!=MULTIBOOT_BOOTLOADER_MAGIC || !(info->flags&(1<<6))) {
        kprintf("Invalid Multiboot memory map\n");
        for(;;) __asm__ volatile("cli; hlt");
    }
    kmemset(bitmap,255,sizeof(bitmap));
    managed_frames = 0;
    uintptr_t cursor=KERNEL_DIRECT_BASE+info->mmap_addr;
    uintptr_t end=cursor+info->mmap_length;
    while(cursor<end) {
        multiboot_memory_map_t *m=(void *)cursor;
        if(end-cursor<sizeof(*m) || m->size<20 || m->size>end-cursor-4) break;
        uint64_t base=((uint64_t)m->addr_high<<32)|m->addr_low;
        uint64_t len=((uint64_t)m->len_high<<32)|m->len_low;
        if(m->type==1 && base<LIMIT) {
            uint64_t top=len>LIMIT-base?LIMIT:base+len;
            if (top / 4096 > managed_frames) managed_frames = top / 4096;
            for(uint64_t p=(base+4095)/4096;p<top/4096;p++) bitmap[p/8]&=~(1U<<(p%8));
        }
        cursor+=m->size+4;
    }
    extern char _kernel_end;
    uintptr_t kernel_end=(uintptr_t)&_kernel_end-KERNEL_LINK_BASE;
    /* Keep low firmware memory and all boot/kernel storage reserved. */
    for(unsigned i=0;i<(kernel_end+4095)/4096;i++) reserve(i);
    /* A firmware framebuffer must never become allocatable RAM. */
    if (info->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
        uint64_t base=info->framebuffer_addr;
        uint64_t bytes=(uint64_t)info->framebuffer_pitch * info->framebuffer_height;
        if (base < LIMIT) {
            uint64_t end=bytes > LIMIT-base ? LIMIT : base+bytes;
            for (uint64_t frame=base/4096; frame<(end+4095)/4096; frame++)
                reserve((unsigned)frame);
        }
    }
    hint=(kernel_end+4095)/4096;
    kprintf("PMM: usable map ceiling=%lu MiB, physical addresses=64-bit\n",
            (unsigned long)managed_frames / 256);
#ifdef PMM_HIGH_TEST
    /* Force subsequent page tables, tasks and DMA allocations into high RAM. */
    hint = (unsigned)(UINT64_C(0x100000000) / 4096);
#endif
}
uintptr_t pmm_alloc_frame(void) {
    for(unsigned j=0;j<managed_frames;j++) {
        unsigned i=(hint+j)%managed_frames;
        if(!(bitmap[i/8]&(1U<<(i%8)))) { reserve(i); hint=(i+1)%managed_frames; return (uintptr_t)i*4096; }
    }
    return 0;
}
void pmm_free_frame(uintptr_t p) {
    if (p >= LIMIT) return;
    unsigned i=p/4096;
    if(p && !(p&4095) && i<FRAMES) { bitmap[i/8]&=~(1U<<(i%8)); if(i<hint) hint=i; }
}
