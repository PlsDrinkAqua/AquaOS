/* Multiboot v1 enters in 32-bit protected mode with paging disabled. */
.set BASE, 0xffffffff80000000
.section .multiboot,"a"
.align 4
.long 0x1badb002, 7, -(0x1badb002 + 7)
/* Address fields unused without the a.out flag; request RGB 640x480x32. */
.long 0, 0, 0, 0, 0
.long 0, 640, 480, 32
.section .boot.text,"ax"
.code32
.global _start
_start:
    cli
    cld
    mov %eax, boot_magic
    mov %ebx, boot_info
    mov $boot_stack_top, %esp
    /* Long mode and SSE2 are required by our x86-64 ABI. */
    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb unsupported
    mov $0x80000001, %eax
    cpuid
    test $(1 << 29), %edx
    jz unsupported
    mov $1, %eax
    cpuid
    test $(1 << 26), %edx
    jz unsupported
    mov $boot_pdpt_low, %eax
    or $3, %eax
    mov %eax, boot_pml4
    mov $boot_pdpt_high, %eax
    or $3, %eax
    mov %eax, boot_pml4 + 511*8
    mov $boot_pd, %eax
    or $3, %eax
    mov %eax, boot_pdpt_low
    mov %eax, boot_pdpt_high + 510*8
    xor %ecx, %ecx
1:  mov %ecx, %eax
    shl $21, %eax
    or $0x83, %eax
    mov %eax, boot_pd(,%ecx,8)
    inc %ecx
    cmp $512, %ecx
    jne 1b
    /* Separate 64-GiB physical window at ffff800000000000 (PML4 256).
     * Kernel code retains its original linked alias in PML4 511. */
    mov $boot_direct_pdpt, %eax
    or $3, %eax
    mov %eax, boot_pml4 + 256*8
    xor %ecx, %ecx
4:  mov %ecx, %eax
    shl $12, %eax
    add $boot_direct_pds, %eax
    or $3, %eax
    mov %eax, boot_direct_pdpt(,%ecx,8)
    inc %ecx
    cmp $64, %ecx
    jne 4b
    xor %ecx, %ecx
5:  mov %ecx, %eax
    shl $21, %eax
    or $0x83, %eax
    mov %eax, boot_direct_pds(,%ecx,8)
    mov %ecx, %edx
    shr $11, %edx
    mov %edx, boot_direct_pds+4(,%ecx,8)
    inc %ecx
    cmp $(64*512), %ecx
    jne 5b
    mov %cr4, %eax
    or $((1<<5) | (1<<9) | (1<<10)), %eax
    mov %eax, %cr4
    mov $boot_pml4, %eax
    mov %eax, %cr3
    mov $0xc0000080, %ecx
    rdmsr
    or $(1<<8), %eax
    wrmsr
    lgdt boot_gdt_ptr
    mov %cr0, %eax
    and $~12, %eax
    or $0x80010022, %eax
    mov %eax, %cr0
    ljmp $8, $long_entry
unsupported:
    movw $0x4f36, 0xb8000
2:  hlt
    jmp 2b
.code64
long_entry:
    mov $16, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    movabs $high_entry, %rax
    jmp *%rax
.align 8
boot_gdt:
    .quad 0, 0x00af9a000000ffff, 0x00cf92000000ffff
boot_gdt_ptr:
    .word 23
    .long boot_gdt
.section .boot.data,"aw"
boot_magic: .long 0
boot_info: .long 0
.section .boot.bss,"aw",@nobits
.align 4096
.global boot_pml4
boot_pml4: .skip 4096
boot_pdpt_low: .skip 4096
boot_pdpt_high: .skip 4096
boot_pd: .skip 4096
boot_direct_pdpt: .skip 4096
boot_direct_pds: .skip 64*4096
.align 16
boot_stack: .skip 16384
boot_stack_top:
.section .text
high_entry:
    movabs $kernel_boot_stack_top, %rsp
    xor %ebp, %ebp
    movabs $(boot_info + BASE), %rax
    mov (%rax), %edi
    movabs $(boot_magic + BASE), %rax
    mov (%rax), %esi
    movabs $(boot_pml4 + BASE), %rax
    movq $0, (%rax) /* Drop the temporary identity map. */
    mov %cr3, %rdx
    mov %rdx, %cr3
    call kernel_main
3:  cli
    hlt
    jmp 3b
.section .bss
.align 16
kernel_boot_stack: .skip 32768
kernel_boot_stack_top:
.section .note.GNU-stack,"",@progbits
