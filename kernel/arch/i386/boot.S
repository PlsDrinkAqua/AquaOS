# Define high-half offset
.set ADDR_OFFSET, 0xC0000000
.set DIRECT_MAP_BYTES, 0x04000000
.set DIRECT_MAP_TABLES, 16

# Multiboot header constants
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set FLAGS,    ALIGN | MEMINFO  # multiboot 'flag' field
.set MAGIC,    0x1BADB002       # boot header magic
.set CHECKSUM, -(MAGIC + FLAGS) # header checksum

.section .multiboot, "aw"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

# Reserve stack for initial thread
.section .bss
.align 16
.global stack_bottom
stack_bottom:
.skip 16384
.global stack_top
stack_top:

# Preallocate paging structures
.section .bss, "aw", @nobits
.align 4096
.globl boot_page_directory
boot_page_directory:
.skip 4096
.globl boot_page_table1
.globl boot_page_tables
boot_page_table1:
boot_page_tables:
.skip DIRECT_MAP_TABLES * 4096

# Kernel entry point
.section .text
.global _start
.type _start, @function
_start:
    # Load physical address of page table
    movl $(boot_page_table1 - ADDR_OFFSET), %edi

    # Build a contiguous 64 MiB physical direct map.
    movl $0, %esi                   # physical address
1:
    cmpl $DIRECT_MAP_BYTES, %esi
    jge 2f                         # done mapping kernel region

    # entry = phys | present | writable
    movl %esi, %edx
    orl $0x003, %edx
    movl %edx, (%edi)

    # advance to next page/table entry
    addl $4096, %esi               # next physical page
    addl $4,    %edi               # next PTE
    jmp 1b

2:
    # Keep a temporary identity mapping for the bootstrap code.
    movl $(boot_page_table1 - ADDR_OFFSET + 0x003), \
         boot_page_directory - ADDR_OFFSET + 0

    # Install the direct-map tables at 0xC0000000.
    movl $(boot_page_directory - ADDR_OFFSET + 768*4), %edi
    movl $(boot_page_tables - ADDR_OFFSET), %esi
    movl $DIRECT_MAP_TABLES, %ecx
3:
    movl %esi, %edx
    orl $0x003, %edx
    movl %edx, (%edi)
    addl $4096, %esi
    addl $4, %edi
    decl %ecx
    jnz 3b

    # Load CR3 with page directory phys address
    movl $(boot_page_directory - ADDR_OFFSET), %ecx
    movl %ecx, %cr3

    # Enable paging + write-protect
    movl %cr0, %ecx
    orl $0x80010000, %ecx
    movl %ecx, %cr0

    # Jump to higher-half code
    lea 4f, %ecx
    jmp *%ecx

4:
    # Unmap identity PDE 0
    movl $0, boot_page_directory + 0

    # Flush TLB by reloading CR3
    movl %cr3, %ecx
    movl %ecx, %cr3

    # Set up stack
    movl $stack_top, %esp

    # Call high-level kernel
    pushl %eax   # magic
    pushl %ebx   # mbd
    call kernel_main

    # If kernel returns, halt
    cli
halt_loop:
    hlt
    jmp halt_loop
.size _start, .- _start
