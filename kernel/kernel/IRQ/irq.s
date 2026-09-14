.global  _irq0
.align   4
_irq0:
   cli
   pushl $0           # Push dummy error code (0)
   pushl $32          # Push interrupt number (32)
   jmp irq_common_stub

.global  _irq1
.align   4
_irq1:
   cli
   pushl $0           # Push dummy error code (0)
   pushl $33          # Push interrupt number (32)
   jmp irq_common_stub

; .global  _irq14
; .align   4
; _irq14:
;     cli
;     pushl $0       # Dummy error code
;     pushl $0x2E    # Interrupt vector 46 (IRQ14 remapped to 0x28+6)
;     jmp irq_common_stub

; .global  _irq15
; .align   4
; _irq15:
;     cli
;     pushl $0       # Dummy error code
;     pushl $0x2F    # Interrupt vector 47 (IRQ15 remapped to 0x28+7)
;     jmp irq_common_stub
.global _isr0
.align 4
_isr0:
    pushl $0
    pushl $0
    jmp exception_common_stub

.global _isr13
.align 4
_isr13:
    pushl $13
    jmp exception_common_stub

.global _isr14
.align 4
_isr14:
    pushl $14
    jmp exception_common_stub

.align 4
exception_common_stub:
    pushal
    cld

    movw %ds, %ax
    pushl %eax

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    movl %esp, %eax
    addl $4, %eax
    pushl %eax
    call exception_handler
    addl $4, %esp
    cli
    movl %esp, %eax
    addl $4, %eax
    pushl %eax
    call task_return_to_user
    cli
    addl $4, %esp

    popl %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    popal
    addl $8, %esp
    iret

    
.global   irq_common_stub
.align   4
irq_common_stub:
    pushal             # Save all general-purpose registers
    cld                # Clear DF for C code

    movw %ds, %ax
    pushl %eax

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    movl %esp, %eax    # EAX points to saved DS slot
    addl $4, %eax      # Skip saved DS slot; now points to interrupt_frame_t
    pushl %eax      	# Push pointer as argument to the C handler
    
    call    interrupt_handler  # Call your C handler
    addl $4, %esp
    cli
    movl %esp, %eax
    addl $4, %eax
    pushl %eax
    call task_return_to_user
    cli
    addl $4, %esp      # Clean up the argument (pointer)

    popl %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    popal              # Restore general-purpose registers
    addl $8, %esp      # Clean up the two pushed values: dummy error code and interrupt number
    iret               # Return from interrupt
