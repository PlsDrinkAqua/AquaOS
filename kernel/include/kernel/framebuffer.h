#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <kernel/multiboot.h>
/* Called after PMM/VMM/heap initialization, before creating process address spaces. */
bool framebuffer_init(const multiboot_info_t *info);
void framebuffer_cell(unsigned x, unsigned y, uint16_t cell);

bool framebuffer_info(uint32_t *w, uint32_t *h);
int framebuffer_show(uint32_t pid, const uint32_t *pixels, uint32_t w, uint32_t h, int32_t x, int32_t y);
void framebuffer_release(uint32_t pid);
