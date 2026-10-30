#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <aquaos/bmp.h>
static unsigned char file[70];
static void u32(unsigned offset,uint32_t value) {
    for(unsigned i=0;i<4;i++) file[offset+i]=value>>(i*8);
}
static void fixture(int top_down) {
    memset(file,0,sizeof(file)); file[0]='B'; file[1]='M';
    u32(2,70); u32(10,54); u32(14,40); u32(18,2); u32(22,top_down ? 0xfffffffe : 2);
    file[26]=1; file[28]=24;
    /* Two padded rows, distinct corners. Bottom-up file first row is blue/white. */
    unsigned char rows[]={255,0,0,255,255,255,0,0, 0,0,255,0,255,0,0,0};
    memcpy(file+54,rows,16);
    if(top_down) { memcpy(file+54,rows+8,8); memcpy(file+62,rows,8); }
}
static void save(const char *path,size_t bytes) {
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(file,1,bytes,f)==bytes); fclose(f);
}
int main(void) {
    char path[]="/tmp/aquaos-bmp-XXXXXX";
    int fd=mkstemp(path); assert(fd>=0); close(fd);
    aquaos_surface image={0};
    assert(aquaos_bmp_load(NULL,&image)<0 && !image.pixels);
    for(int top=0;top<2;top++) {
        fixture(top); save(path,70); assert(aquaos_bmp_load(path,&image)==0);
        assert(image.width==2 && image.height==2 && image.stride==2);
        assert(image.pixels[0]==0xff0000 && image.pixels[1]==0x00ff00);
        assert(image.pixels[2]==0x0000ff && image.pixels[3]==0xffffff); free(image.pixels);
    }
    for(unsigned n=0;n<70;n++) {
        fixture(0); save(path,n); assert(aquaos_bmp_load(path,&image)<0 && !image.pixels);
    }
    const unsigned offsets[]={2,10,14,18,22,26,28,30,34};
    const uint32_t values[]={0,0xffffffff,124,0xffffffff,0x80000000,0,32,1,1};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++) {
        fixture(0); u32(offsets[i],values[i]); save(path,70);
        assert(aquaos_bmp_load(path,&image)<0 && !image.pixels);
    }
    unlink(path);
    puts("BMP orientation, colors, row padding, truncation and invalid headers passed.");
}
