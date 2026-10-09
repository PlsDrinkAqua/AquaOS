OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
BASE = 0xffffffff80000000;
SECTIONS {
    . = 1M;
    _kernel_start = .;
    .multiboot : { KEEP(*(.multiboot)) }
    .boot.text ALIGN(4096) : { *(.boot.text) }
    .boot.data ALIGN(4096) : { *(.boot.data) }
    .boot.bss ALIGN(4096) (NOLOAD) : { *(.boot.bss) }
    . += BASE;
    .text ALIGN(4096) : AT(ADDR(.text) - BASE) { *(.text*) }
    .rodata ALIGN(4096) : AT(ADDR(.rodata) - BASE) { *(.rodata*) }
    .data ALIGN(4096) : AT(ADDR(.data) - BASE) { *(.data*) }
    .bss ALIGN(4096) (NOLOAD) : AT(ADDR(.bss) - BASE) { *(COMMON) *(.bss*) }
    _kernel_end = .;
    boot_page_directory = boot_pml4 + BASE;
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
