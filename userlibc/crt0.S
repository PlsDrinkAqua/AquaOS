.section .text
.global _start
.type _start, @function
_start:
    xorl %ebp, %ebp
    movl (%esp), %eax
    leal 4(%esp), %edx
    andl $-16, %esp
    subl $8, %esp
    pushl %edx
    pushl %eax
    call main
    addl $16, %esp
    subl $12, %esp
    pushl %eax
    call exit
1:  jmp 1b
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
