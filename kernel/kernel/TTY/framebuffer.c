#include <kernel/framebuffer.h>
#include <kernel/layout.h>
#include <kernel/vmm.h>
#include "font8x16.h"

/* Separate from E1000's device window. No user mapping of physical memory. */
#define FB_WINDOW (KERNEL_MMIO_BASE + 0x1000000UL)
#define FB_MAX_BYTES (16UL * 1024 * 1024)
static volatile uint32_t *pixels;
static uint32_t stride, left, top;
static uint32_t colors[16];

bool framebuffer_init(const multiboot_info_t *m) {
    if (!m || !(m->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) ||
        m->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB ||
        m->framebuffer_bpp != 32 || m->framebuffer_width < 640 ||
        m->framebuffer_height < 400 || m->framebuffer_width > 4096 ||
        m->framebuffer_height > 4096 || (m->framebuffer_addr & 3) ||
        (m->framebuffer_pitch & 3) ||
        m->framebuffer_pitch < m->framebuffer_width * 4) return false;
    unsigned rp=m->framebuffer_red_field_position, gp=m->framebuffer_green_field_position;
    unsigned bp=m->framebuffer_blue_field_position;
    if (m->framebuffer_red_mask_size != 8 || m->framebuffer_green_mask_size != 8 ||
        m->framebuffer_blue_mask_size != 8 || rp > 24 || gp > 24 || bp > 24) return false;
    uint32_t rm=255U<<rp, gm=255U<<gp, bm=255U<<bp;
    if ((rm & gm) || (rm & bm) || (gm & bm)) return false;
    uint64_t bytes=(uint64_t)m->framebuffer_pitch * m->framebuffer_height;
    uint64_t offset=m->framebuffer_addr & 4095;
    if (bytes > FB_MAX_BYTES-offset || m->framebuffer_addr >= (UINT64_C(1)<<52) ||
        bytes > (UINT64_C(1)<<52)-m->framebuffer_addr) return false;
    uintptr_t end=FB_WINDOW + ((bytes+offset+4095)&~UINT64_C(4095));
    if (vmm_map_region(FB_WINDOW,end,m->framebuffer_addr & ~UINT64_C(4095),
                       3 | VMM_CACHE_DISABLE | VMM_WRITE_THROUGH) < 0) return false;
    pixels=(volatile uint32_t *)(FB_WINDOW+offset);
    stride=m->framebuffer_pitch/4;
    left=(m->framebuffer_width-640)/2;
    top=(m->framebuffer_height-400)/2;
    static const uint32_t rgb[16]={0x000000,0x0000aa,0x00aa00,0x00aaaa,
        0xaa0000,0xaa00aa,0xaa5500,0xaaaaaa,0x555555,0x5555ff,0x55ff55,
        0x55ffff,0xff5555,0xff55ff,0xffff55,0xffffff};
    for (unsigned i=0;i<16;i++)
        colors[i]=((rgb[i]>>16)<<rp) | (((rgb[i]>>8)&255)<<gp) | ((rgb[i]&255)<<bp);
    for (unsigned y=0;y<m->framebuffer_height;y++)
        for (unsigned x=0;x<m->framebuffer_width;x++) pixels[y*stride+x]=0;
    return true;
}

void framebuffer_cell(unsigned x,unsigned y,uint16_t cell) {
    if (!pixels || x>=80 || y>=25) return;
    unsigned ch=cell&255;
    if (ch>=128) ch='?';
    uint32_t fg=colors[(cell>>8)&15], bg=colors[(cell>>12)&7];
    for (unsigned row=0;row<16;row++) {
        unsigned bits=font8x16[ch][row];
        volatile uint32_t *dst=pixels+(top+y*16+row)*stride+left+x*8;
        for (unsigned col=0;col<8;col++) dst[col]=(bits & (0x80U>>col)) ? fg : bg;
    }
}
