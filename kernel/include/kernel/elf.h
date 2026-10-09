#pragma once
#include <stdint.h>
#include <stddef.h>
#include <kernel/paging.h>

/* ===== ELF fixed-width types ===== */
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Off;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;

/* ===== ELF header ===== */
#define ELF_NIDENT 16

typedef struct {
    uint8_t     e_ident[ELF_NIDENT];
    Elf32_Half  e_type;
    Elf32_Half  e_machine;
    Elf32_Word  e_version;
    Elf32_Addr  e_entry;
    Elf32_Off   e_phoff;
    Elf32_Off   e_shoff;
    Elf32_Word  e_flags;
    Elf32_Half  e_ehsize;
    Elf32_Half  e_phentsize;
    Elf32_Half  e_phnum;
    Elf32_Half  e_shentsize;
    Elf32_Half  e_shnum;
    Elf32_Half  e_shstrndx;
} Elf32_Ehdr;

/* ===== Program header ===== */
typedef struct {
    Elf32_Word  p_type;
    Elf32_Off   p_offset;
    Elf32_Addr  p_vaddr;
    Elf32_Addr  p_paddr;
    Elf32_Word  p_filesz;
    Elf32_Word  p_memsz;
    Elf32_Word  p_flags;
    Elf32_Word  p_align;
} Elf32_Phdr;

/* ===== e_ident indexes ===== */
enum {
    EI_MAG0       = 0,
    EI_MAG1       = 1,
    EI_MAG2       = 2,
    EI_MAG3       = 3,
    EI_CLASS      = 4,
    EI_DATA       = 5,
    EI_VERSION    = 6,
    EI_OSABI      = 7,
    EI_ABIVERSION = 8
};

/* ===== Magic ===== */
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

/* ===== Class / endian / version ===== */
#define ELFCLASS32   1
#define ELFDATA2LSB  1
#define EV_CURRENT   1

/* ===== File type ===== */
#define ET_NONE  0
#define ET_REL   1
#define ET_EXEC  2
#define ET_DYN   3

/* ===== Machine ===== */
#define EM_386   3

/* ===== Program header types ===== */
#define PT_NULL    0
#define PT_LOAD    1

/* ===== Section header ===== */
typedef struct {
    Elf32_Word sh_name;
    Elf32_Word sh_type;
    Elf32_Word sh_flags;
    Elf32_Addr sh_addr;
    Elf32_Off  sh_offset;
    Elf32_Word sh_size;
    Elf32_Word sh_link;
    Elf32_Word sh_info;
    Elf32_Word sh_addralign;
    Elf32_Word sh_entsize;
} Elf32_Shdr;

/* ===== Symbol table entry ===== */
typedef struct {
    Elf32_Word  st_name;
    Elf32_Addr  st_value;
    Elf32_Word  st_size;
    uint8_t     st_info;
    uint8_t     st_other;
    Elf32_Half  st_shndx;
} Elf32_Sym;

/* ===== Relocation entries ===== */
typedef struct {
    Elf32_Addr r_offset;
    Elf32_Word r_info;
} Elf32_Rel;

#define SHT_REL     9
#define SHT_DYNSYM  11
#define SHT_SYMTAB  2

#define SHN_UNDEF   0

#define ELF32_R_SYM(info)  ((info) >> 8)
#define ELF32_R_TYPE(info) ((uint8_t)(info))

#define R_386_NONE      0
#define R_386_32        1
#define R_386_PC32      2
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8

/* ===== Segment flags ===== */
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

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
