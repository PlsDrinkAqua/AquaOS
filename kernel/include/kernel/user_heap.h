#ifndef USER_HEAP_H
#define USER_HEAP_H

#include <stdint.h>

void user_heap_init(void);
void *sys_sbrk(intptr_t increment);
uintptr_t user_heap_start_vaddr(void);
uintptr_t user_heap_brk_vaddr(void);
uintptr_t user_heap_limit_vaddr(void);

#endif
