#ifndef _PMM
#define _PMM

#include "multiboot.h"
#include <stdint.h>

void pmm_init(multiboot_info_t* mbd, uint32_t magic);
uintptr_t pmm_alloc_frame(void);
void pmm_free_frame(uintptr_t physaddr);
void pmm_test_frame(uintptr_t physaddr);


#endif