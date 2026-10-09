#include <kernel/pmm.h>
#include <kernel/layout.h>
#include <libk/string.h>
#include <libk/stdio.h>
/* First implementation manages the 1 GiB bootstrap direct-map window. */
#define LIMIT (1024UL*1024*1024)
#define FRAMES (LIMIT/4096)
static uint8_t bitmap[FRAMES/8];
static unsigned hint;
static void reserve(unsigned n) { bitmap[n/8]|=1U<<(n%8); }
void pmm_init(multiboot_info_t *physical_info,uint32_t magic) {
    multiboot_info_t *info=(void *)(KERNEL_DIRECT_BASE+(uintptr_t)physical_info);
    if(magic!=MULTIBOOT_BOOTLOADER_MAGIC || !(info->flags&(1<<6))) {
        kprintf("Invalid Multiboot memory map\n");
        for(;;) __asm__ volatile("cli; hlt");
    }
    kmemset(bitmap,255,sizeof(bitmap));
    uintptr_t cursor=KERNEL_DIRECT_BASE+info->mmap_addr;
    uintptr_t end=cursor+info->mmap_length;
    while(cursor<end) {
        multiboot_memory_map_t *m=(void *)cursor;
        if(end-cursor<sizeof(*m) || m->size<20 || m->size>end-cursor-4) break;
        uint64_t base=((uint64_t)m->addr_high<<32)|m->addr_low;
        uint64_t len=((uint64_t)m->len_high<<32)|m->len_low;
        if(m->type==1 && base<LIMIT) {
            uint64_t top=len>LIMIT-base?LIMIT:base+len;
            for(uint64_t p=(base+4095)/4096;p<top/4096;p++) bitmap[p/8]&=~(1U<<(p%8));
        }
        cursor+=m->size+4;
    }
    extern char _kernel_end;
    uintptr_t kernel_end=(uintptr_t)&_kernel_end-KERNEL_DIRECT_BASE;
    /* Keep low firmware memory and all boot/kernel storage reserved. */
    for(unsigned i=0;i<(kernel_end+4095)/4096;i++) reserve(i);
    hint=(kernel_end+4095)/4096;
}
uint32_t pmm_alloc_frame(void) {
    for(unsigned j=0;j<FRAMES;j++) {
        unsigned i=(hint+j)%FRAMES;
        if(!(bitmap[i/8]&(1U<<(i%8)))) { reserve(i); hint=(i+1)%FRAMES; return i*4096; }
    }
    return 0;
}
void pmm_free_frame(uint32_t p) {
    unsigned i=p/4096;
    if(p && !(p&4095) && i<FRAMES) { bitmap[i/8]&=~(1U<<(i%8)); if(i<hint) hint=i; }
}
