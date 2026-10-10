#pragma once
#include <stdint.h>
#include <stddef.h>
#include <kernel/paging.h>

typedef struct {
    uintptr_t entry;       /* e_entry */
    uintptr_t heap_start;  /* 对齐后的 program_end，可给 brk 使用 */
    uintptr_t heap_end;    /* 初始等于 heap_start */
} elf_load_result_t;

/* 从内存中的 ELF 映像加载到当前用户地址空间 */
int elf_load_from_memory(const void *image, size_t image_size, elf_load_result_t *out);

/* 从 ext2 路径读取并加载 */
int elf_load_from_file(const char *path, elf_load_result_t *out);

/* 从 ext2 路径读取，并加载到指定地址空间 */
int elf_load_from_file_into_address_space(const char *path,
                                          page_directory_t *pd,
                                          elf_load_result_t *out);
