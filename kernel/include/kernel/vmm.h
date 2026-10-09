#ifndef _VMM
#define _VMM

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kernel/paging.h>

void vmm_init();

page_directory_t *vmm_create_address_space(void);
page_directory_t *vmm_clone_address_space(page_directory_t *source);

void vmm_destroy_address_space(page_directory_t *pd);

void vmm_switch_address_space(page_directory_t *pd);

page_directory_t *vmm_current_address_space(void);

void vmm_sync_kernel_mappings(page_directory_t *pd);

int vmm_map_page(uintptr_t vaddr, uintptr_t paddr, uint32_t flags);

uint32_t vmm_translate(uintptr_t vaddr);

/* Check that every page in a userspace range is present and accessible. */
bool vmm_user_range_valid(const void *ptr, size_t size, bool writable);

int vmm_unmap_page(uintptr_t vaddr, bool free_frame);

int vmm_map_region(uintptr_t vstart,
                   uintptr_t vend,
                   uintptr_t pstart,
                   uint32_t flags);

int vmm_unmap_region(uintptr_t vstart,
                     uintptr_t vend,
                     bool free_frames);

// void vmm_test();

void vmm_test_region(void);

typedef struct {
    bool     present;   // PTE.P present?
    bool     writable;  // PTE.RW writable?
    bool     user;      // PTE.U user-accessible?
    uintptr_t paddr;    // 物理页基址（仅当 present 时有效）
} vmm_page_info_t;

void dump_boot_pte(uint32_t va);


#endif
