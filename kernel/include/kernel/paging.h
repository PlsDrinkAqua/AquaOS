#ifdef __x86_64__
#pragma once
#include <stdint.h>
typedef struct __attribute__((aligned(4096))) { uint64_t entries[512]; } page_directory_t;
#else
#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_DIR_ENTRIES 1024
#define PAGE_TABLE_ENTRIES 1024

// 页目录项（PDE）
typedef struct __attribute__((packed)) {
    uint32_t present     : 1;  // P
    uint32_t rw          : 1;  // R/W
    uint32_t user        : 1;  // U/S
    uint32_t pwt         : 1;  // PWT
    uint32_t pcd         : 1;  // PCD
    uint32_t accessed    : 1;  // A
    uint32_t dirty       : 1;  // 如果 PS=0，这位保留；如果 PS=1，表示大页“dirty”
    uint32_t page_size   : 1;  // PS (0 = 4 KiB page table, 1 = 4 MiB page)
    uint32_t global      : 1;  // G (only if PS=1)
    uint32_t avail       : 3;  // 软件可用
    uint32_t frame       :20;  // 物理页框号（高 20 位）
} page_directory_entry_t;

// 页表项（PTE）
typedef struct __attribute__((packed)) {
    uint32_t present   : 1;  // P
    uint32_t rw        : 1;  // R/W
    uint32_t user      : 1;  // U/S
    uint32_t pwt       : 1;  // PWT
    uint32_t pcd       : 1;  // PCD
    uint32_t accessed  : 1;  // A
    uint32_t dirty     : 1;  // D
    uint32_t pat       : 1;  // PAT (only if 4 KiB page)
    uint32_t global    : 1;  // G
    uint32_t avail     : 3;  // 软件可用
    uint32_t frame     :20;  // 物理页框号（高 20 位）
} page_table_entry_t;

// 页表：一页 4 KiB，共 1024 个 PTE
typedef struct __attribute__((aligned(4096))) {
    page_table_entry_t pages[PAGE_TABLE_ENTRIES];
} page_table_t;


typedef struct __attribute__((aligned(4096))) {
  page_directory_entry_t entries[PAGE_DIR_ENTRIES];
} page_directory_t;


#endif // PAGING_H

#endif
