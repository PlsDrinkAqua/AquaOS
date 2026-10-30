#include <kernel/framebuffer.h>
#include <kernel/layout.h>
#include <kernel/vmm.h>
#include "font8x16.h"
#include <kernel/kmalloc.h>
#include <kernel/tty.h>
#include <kernel/errno.h>
#include <aquaos/surface.h>

/* Separate from E1000's device window. No user mapping of physical memory. */
#define FB_WINDOW (KERNEL_MMIO_BASE + 0x1000000UL)
#define FB_MAX_BYTES (16UL * 1024 * 1024)
static volatile uint32_t *pixels;
static uint32_t stride, left, top;
static uint32_t colors[16];
static aquaos_surface back;
static unsigned red_pos, green_pos, blue_pos;
static uint32_t owner;

/* Copy a clipped rectangle from RAM to the device, respecting device pitch. */
static void present(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    int64_t x0=x,y0=y,x1=x0+w,y1=y0+h;
    if (!pixels) return;
    if(x0<0) x0=0;
    if(y0<0) y0=0;
    if(x1>back.width) x1=back.width;
    if(y1>back.height) y1=back.height;
    for(int64_t row=y0;row<y1;row++) for(int64_t col=x0;col<x1;col++) {
        uint32_t rgb=back.pixels[(size_t)row*back.stride+col];
        pixels[(size_t)row*stride+col]=((rgb>>16)<<red_pos) |
            (((rgb>>8)&255)<<green_pos) | ((rgb&255)<<blue_pos);
    }
}

bool framebuffer_info(uint32_t *w,uint32_t *h) {
    if(!pixels) return false;
    *w=back.width; *h=back.height; return true;
}
int framebuffer_show(uint32_t pid,const uint32_t *src,uint32_t w,uint32_t h,int32_t x,int32_t y) {
    if(!pixels) return -K_ENODEV;
    if(owner && owner!=pid) return -K_EBUSY;
    owner=pid;
    aquaos_fill_rect(&back,0,0,back.width,back.height,0);
    aquaos_surface image={(uint32_t *)src,w,h,w};
    aquaos_blit(&back,&image,x,y);
    present(0,0,back.width,back.height);
    return 0;
}
void framebuffer_release(uint32_t pid) {
    if(!owner || owner!=pid) return;
    owner=0;
    aquaos_fill_rect(&back,0,0,back.width,back.height,0);
    present(0,0,back.width,back.height);
    terminal_refresh();
}

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
    uint32_t *storage=kmalloc((size_t)m->framebuffer_width*m->framebuffer_height*4);
    if (!storage) return false;
    uintptr_t end=FB_WINDOW + ((bytes+offset+4095)&~UINT64_C(4095));
    if (vmm_map_region(FB_WINDOW,end,m->framebuffer_addr & ~UINT64_C(4095),
                       3 | VMM_CACHE_DISABLE | VMM_WRITE_THROUGH) < 0) { kfree(storage); return false; }
    pixels=(volatile uint32_t *)(FB_WINDOW+offset);
    stride=m->framebuffer_pitch/4;
    back=(aquaos_surface){storage,m->framebuffer_width,m->framebuffer_height,m->framebuffer_width};
    red_pos=rp; green_pos=gp; blue_pos=bp;
    left=(m->framebuffer_width-640)/2;
    top=(m->framebuffer_height-400)/2;
    static const uint32_t rgb[16]={0x000000,0x0000aa,0x00aa00,0x00aaaa,
        0xaa0000,0xaa00aa,0xaa5500,0xaaaaaa,0x555555,0x5555ff,0x55ff55,
        0x55ffff,0xff5555,0xff55ff,0xffff55,0xffffff};
    for (unsigned i=0;i<16;i++)
        colors[i]=rgb[i];
    aquaos_fill_rect(&back,0,0,back.width,back.height,0);
    present(0,0,back.width,back.height);
    return true;
}

void framebuffer_cell(unsigned x,unsigned y,uint16_t cell) {
    if (!pixels || owner || x>=80 || y>=25) return;
    unsigned ch=cell&255;
    if (ch>=128) ch='?';
    uint32_t fg=colors[(cell>>8)&15], bg=colors[(cell>>12)&7];
    for (unsigned row=0;row<16;row++) {
        unsigned bits=font8x16[ch][row];
        uint32_t *dst=back.pixels+(top+y*16+row)*back.stride+left+x*8;
        for (unsigned col=0;col<8;col++) dst[col]=(bits & (0x80U>>col)) ? fg : bg;
    }
    present(left+x*8,top+y*16,8,16);
}
