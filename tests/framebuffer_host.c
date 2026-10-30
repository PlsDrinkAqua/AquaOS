/* cc -I kernel/include tests/framebuffer_host.c -o /tmp/fbcheck && /tmp/fbcheck */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/kernel/TTY/framebuffer.c"
static unsigned maps;
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
    assert(!framebuffer_init(&m) && maps==1); /* Failed mapping is not activated. */
    framebuffer_cell(0,0,'A');
    uint32_t screen[18*12];
    for(unsigned i=0;i<18*12;i++) screen[i]=0xdeadbeef;
    pixels=screen; stride=12; left=2; top=1;
    colors[7]=0xffffff; colors[1]=0xaa;
    framebuffer_cell(0,0,0x1741);
    unsigned foreground=0;
    for(unsigned y=0;y<18;y++) for(unsigned x=0;x<12;x++) {
        uint32_t v=screen[y*12+x];
        if(y>=1 && y<17 && x>=2 && x<10) {
            uint32_t expected=(font8x16['A'][y-1] & (0x80>>(x-2))) ? 0xffffff : 0xaa;
            assert(v==expected); foreground+=v==0xffffff;
        } else assert(v==0xdeadbeef);
    }
    assert(foreground);
    framebuffer_cell(80,0,'A'); framebuffer_cell(0,25,'A');
    puts("Framebuffer format, mapping failure, glyph and pitch checks passed.");
}
