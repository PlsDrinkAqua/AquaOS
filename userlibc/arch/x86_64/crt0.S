.text
.global _start
.type _start,@function
_start:
    xor %ebp,%ebp
    mov (%rsp),%rdi
    lea 8(%rsp),%rsi
    and $-16,%rsp
    call main
    mov %eax,%edi
    call exit
1:  jmp 1b
.section .note.GNU-stack,"",@progbits
