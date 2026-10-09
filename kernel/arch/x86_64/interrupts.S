.text
.global load_gdt64
load_gdt64:
    lgdt (%rdi)
    pushq $8
    lea 1f(%rip), %rax
    push %rax
    lretq
1:  mov $16, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %eax, %eax
    mov %ax, %fs
    mov %ax, %gs
    ret
.global jump_usermode
jump_usermode:
    cli
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    pushq $0x23
    push %rsi
    pushq $0x202
    pushq $0x1b
    push %rdi
    iretq
.altmacro
.macro VECTOR n
isr64_\n:
.if !(\n == 8 || \n == 10 || \n == 11 || \n == 12 || \n == 13 || \n == 14 || \n == 17 || \n == 21 || \n == 29 || \n == 30)
    pushq $0
.endif
    pushq $\n
    jmp common64
.endm
.set vector, 0
.rept 256
    VECTOR %vector
    .set vector, vector + 1
.endr
common64:
    cld
    push %rax
    push %rcx
    push %rdx
    push %rbx
    pushq $0
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, %rdi
    mov %rsp, %rbx
    and $-16, %rsp
    call dispatch64
    cli
    mov %rbx, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    add $8, %rsp
    pop %rbx
    pop %rdx
    pop %rcx
    pop %rax
    add $16, %rsp
    iretq
.section .rodata
.global isr64_table
isr64_table:
.macro VECTOR_ADDRESS n
    .quad isr64_\n
.endm
.set vector, 0
.rept 256
    VECTOR_ADDRESS %vector
    .set vector, vector + 1
.endr
.section .note.GNU-stack,"",@progbits
