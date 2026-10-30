#pragma once
#include <stdint.h>
#include <stddef.h>
/* XRGB8888 in ordinary RAM: color 0x00RRGGBB. Stride is in pixels.
 * Caller owns storage of at least stride*height pixels, stride >= width.
 * Source and destination storage must not overlap for blit. */
typedef struct {
    uint32_t *pixels;
    uint32_t width, height, stride;
} aquaos_surface;

static inline void aquaos_fill_rect(aquaos_surface *s, int32_t x, int32_t y,
                                    uint32_t w, uint32_t h, uint32_t color) {
    int64_t x0=x, y0=y, x1=x0+w, y1=y0+h;
    if (x0<0) x0=0;
    if (y0<0) y0=0;
    if (x1>s->width) x1=s->width;
    if (y1>s->height) y1=s->height;
    for (int64_t row=y0;row<y1;row++)
        for (int64_t col=x0;col<x1;col++)
            s->pixels[(size_t)row*s->stride+col]=color & 0xffffff;
}

static inline void aquaos_blit(aquaos_surface *dst, const aquaos_surface *src,
                               int32_t x, int32_t y) {
    int64_t x0=x, y0=y, x1=x0+src->width, y1=y0+src->height;
    if (x0<0) x0=0;
    if (y0<0) y0=0;
    if (x1>dst->width) x1=dst->width;
    if (y1>dst->height) y1=dst->height;
    for (int64_t row=y0;row<y1;row++)
        for (int64_t col=x0;col<x1;col++)
            dst->pixels[(size_t)row*dst->stride+col]=
                src->pixels[(size_t)(row-y)*src->stride+(col-x)] & 0xffffff;
}
