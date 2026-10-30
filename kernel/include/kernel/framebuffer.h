#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <kernel/multiboot.h>
/* Called after PMM/VMM initialization, before creating process address spaces. */
bool framebuffer_init(const multiboot_info_t *info);
void framebuffer_cell(unsigned x, unsigned y, uint16_t cell);
