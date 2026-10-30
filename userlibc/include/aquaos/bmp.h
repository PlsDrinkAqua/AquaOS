#pragma once
#include <aquaos/surface.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Load uncompressed 24-bit BITMAPINFOHEADER BMP into malloc-owned XRGB8888.
 * Supports bottom-up/top-down rows and padding. Max 2048 per dimension and
 * 1,048,576 pixels. On failure returns -1/errno and an empty surface.
 * Free image->pixels after use. */
int aquaos_bmp_load(const char *path, aquaos_surface *image);
#ifdef __cplusplus
}
#endif
