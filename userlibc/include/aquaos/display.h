#pragma once
#include <stdint.h>
/* Native ABI. Pixels are packed XRGB8888 (0x00RRGGBB), no physical addresses.
 * show temporarily owns the screen; only the foreground terminal job may call.
 * Text/history continues to update while hidden. Restore, exit and successful
 * exec release ownership and repaint the terminal. No GPU or vsync guarantee. */
typedef struct { uint32_t width, height; } aquaos_display_info;
typedef struct {
    uint64_t pixels;
    uint32_t width, height;
    int32_t x, y;
} aquaos_display_request;
#ifdef __cplusplus
extern "C" {
#endif
int aquaos_display_get_info(aquaos_display_info *info);
int aquaos_display_show(const uint32_t *pixels, uint32_t width, uint32_t height,
                        int32_t x, int32_t y);
int aquaos_display_restore(void);
#ifdef __cplusplus
}
#endif
