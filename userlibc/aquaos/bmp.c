#include <aquaos/bmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1]<<8; }

int aquaos_bmp_load(const char *path,aquaos_surface *image) {
    if(!image) { errno=EINVAL; return -1; }
    *image=(aquaos_surface){0};
    if(!path) { errno=EINVAL; return -1; }
    FILE *f=fopen(path,"rb");
    if(!f) return -1;
    int error=EINVAL;
    unsigned char header[54], *row=NULL;
    uint32_t *pixels=NULL;
    if(fseek(f,0,SEEK_END)<0) { error=EIO; goto fail; }
    long size=ftell(f);
    if(size<54 || fseek(f,0,SEEK_SET)<0 || fread(header,1,54,f)!=54) goto fail;
    uint32_t declared=le32(header+2), offset=le32(header+10);
    uint32_t w=le32(header+18), raw_h=le32(header+22);
    int top_down=(raw_h & 0x80000000U)!=0;
    uint32_t h=top_down ? 0U-raw_h : raw_h;
    if(header[0]!='B' || header[1]!='M' || le32(header+14)!=40 ||
       le16(header+26)!=1 || le16(header+28)!=24 || le32(header+30)!=0 ||
       !w || !h || w>2048 || h>2048 || (uint64_t)w*h>1048576 || offset<54) goto fail;
    uint32_t row_bytes=(w*3+3)&~3U;
    uint64_t end=(uint64_t)offset+(uint64_t)row_bytes*h;
    if(end>declared || declared>(uint64_t)size) goto fail;
    uint32_t image_bytes=le32(header+34);
    if(image_bytes && image_bytes!=(uint64_t)row_bytes*h) goto fail;
    pixels=malloc((size_t)w*h*4);
    row=malloc(row_bytes);
    if(!pixels || !row) { error=ENOMEM; goto fail; }
    if(fseek(f,offset,SEEK_SET)<0) { error=EIO; goto fail; }
    for(uint32_t i=0;i<h;i++) {
        if(fread(row,1,row_bytes,f)!=row_bytes) { error=EIO; goto fail; }
        uint32_t y=top_down ? i : h-1-i;
        for(uint32_t x=0;x<w;x++) {
            const unsigned char *p=row+x*3;
            pixels[(size_t)y*w+x]=(uint32_t)p[2]<<16 | (uint32_t)p[1]<<8 | p[0];
        }
    }
    free(row); fclose(f);
    *image=(aquaos_surface){pixels,w,h,w};
    return 0;
fail:
    free(row); free(pixels); fclose(f); errno=error; return -1;
}
