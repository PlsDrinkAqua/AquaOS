/* cc -I kernel/include -idirafter userlibc/include tests/framebuffer_host.c -o /tmp/fbcheck */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "../kernel/kernel/TTY/framebuffer.c"
static unsigned maps, refreshes;
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }
void terminal_refresh(void) { refreshes++; }
int vmm_map_region(uintptr_t a, uintptr_t b, uintptr_t p, uint32_t flags) {
    (void)a; (void)b; (void)p; (void)flags; maps++; return -1;
}
int main(void) {
    multiboot_info_t m={0};
    assert(!framebuffer_init(&m) && !maps);
    m.flags=MULTIBOOT_INFO_FRAMEBUFFER_INFO;
    m.framebuffer_type=MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
    m.framebuffer_bpp=32;
    m.framebuffer_width=640; m.framebuffer_height=480;
    m.framebuffer_pitch=2560; m.framebuffer_addr=0xfd000000;
    m.framebuffer_red_mask_size=m.framebuffer_green_mask_size=m.framebuffer_blue_mask_size=8;
    m.framebuffer_red_field_position=16; m.framebuffer_green_field_position=8;
    m.framebuffer_pitch=2559;
    assert(!framebuffer_init(&m) && !maps);
    m.framebuffer_pitch=2560; m.framebuffer_blue_field_position=8;
    assert(!framebuffer_init(&m) && !maps);
    m.framebuffer_blue_field_position=0;
    assert(!framebuffer_init(&m) && maps==1);
    framebuffer_cell(0,0,'A');
    uint32_t screen[18*14], storage[18*12];
    for(unsigned i=0;i<18*14;i++) screen[i]=0xdeadbeef;
    for(unsigned i=0;i<18*12;i++) storage[i]=0x123456;
    pixels=screen; stride=14; left=2; top=1;
    back=(aquaos_surface){storage,12,18,12};
    red_pos=16; green_pos=8; blue_pos=0;
    colors[7]=0xffffff; colors[1]=0xaa;
    framebuffer_cell(0,0,0x1741);
    unsigned foreground=0;
    for(unsigned y=0;y<18;y++) for(unsigned x=0;x<14;x++) {
        uint32_t v=screen[y*14+x];
        if(y>=1 && y<17 && x>=2 && x<10) {
            uint32_t expected=(font8x16['A'][y-1] & (0x80>>(x-2))) ? 0xffffff : 0xaa;
            assert(v==expected && storage[y*12+x]==expected); foreground+=v==0xffffff;
        } else assert(v==0xdeadbeef);
    }
    assert(foreground);
    framebuffer_cell(80,0,'A'); framebuffer_cell(0,25,'A');
    aquaos_fill_rect(&back,-2,-1,3,2,0xff0000);
    assert(storage[0]==0xff0000 && storage[1]==0x123456);
    assert(screen[0]==0xdeadbeef); /* RAM drawing does not submit implicitly. */
    present(-2,-1,3,2);
    assert(screen[0]==0xff0000 && screen[1]==0xdeadbeef);
    aquaos_fill_rect(&back,INT32_MAX,INT32_MIN,UINT32_MAX,1,0);
    uint32_t src[]={0xff0000,0x00ff00,0x0000ff,0xffffff};
    aquaos_surface image={src,2,2,2};
    aquaos_blit(&back,&image,-1,-1);
    assert(storage[0]==0xffffff);
    assert(framebuffer_show(1,src,2,2,-1,-1)==0);
    assert(screen[0]==0xffffff && screen[1]==0 && owner==1);
    framebuffer_cell(0,0,0x1741); /* Terminal must not paint over the image. */
    assert(screen[0]==0xffffff);
    assert(framebuffer_show(2,src,2,2,0,0)==-K_EBUSY);
    framebuffer_release(2); assert(owner==1 && !refreshes);
    framebuffer_release(1); assert(!owner && refreshes==1 && screen[0]==0);
    red_pos=0; green_pos=8; blue_pos=16;
    storage[0]=0x123456; present(0,0,1,1); assert(screen[0]==0x563412);
    for(unsigned y=0;y<18;y++) assert(screen[y*14+12]==0xdeadbeef && screen[y*14+13]==0xdeadbeef);
    puts("Framebuffer clipping, backbuffer, pitch, channel conversion and ownership passed.");
}
