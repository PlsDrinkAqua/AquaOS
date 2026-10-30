#include <aquaos/bmp.h>
#include <aquaos/display.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

int main(int argc,char **argv) {
    if(argc!=2) { fprintf(stderr,"Usage: bmpview <24-bit.bmp>\n"); return 1; }
    aquaos_display_info display;
    if(aquaos_display_get_info(&display)<0) {
        fprintf(stderr,"bmpview: framebuffer unavailable (errno %d)\n",errno); return 1;
    }
    aquaos_surface image;
    if(aquaos_bmp_load(argv[1],&image)<0) {
        fprintf(stderr,"bmpview: cannot load 24-bit BMP: %s (errno %d)\n",argv[1],errno); return 1;
    }
    printf("bmpview: %ux%u; press any key to return\n",image.width,image.height);
    fflush(stdout);
    /* Center smaller images; clip larger ones at the right/bottom edges. */
    int32_t x=image.width<display.width ? (display.width-image.width)/2 : 0;
    int32_t y=image.height<display.height ? (display.height-image.height)/2 : 0;
    int result=aquaos_display_show(image.pixels,image.width,image.height,x,y);
    free(image.pixels);
    if(result<0) { fprintf(stderr,"bmpview: display failed (errno %d)\n",errno); return 1; }
    char key;
    result=read(0,&key,1);
    aquaos_display_restore();
    return result<0 ? 1 : 0;
}
