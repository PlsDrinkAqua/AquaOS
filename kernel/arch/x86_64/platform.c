#include <stdint.h>
#include <stddef.h>
#include <kernel/interrupt_frame.h>
#include <kernel/task.h>
#include <libk/string.h>

/* Keep this synchronized with interrupts.S push order and the CPU IRET frame. */
_Static_assert(sizeof(interrupt_frame_t) == 23 * 8, "64-bit interrupt frame size");
_Static_assert(__builtin_offsetof(interrupt_frame_t, eax) == 15 * 8, "RAX frame offset");
_Static_assert(__builtin_offsetof(interrupt_frame_t, eip) == 18 * 8, "RIP frame offset");
static uint64_t gdt[7];
struct table_pointer { uint16_t limit; uint64_t base; } __attribute__((packed));
struct tss64 {
    uint32_t reserved;
    uint64_t rsp[3], reserved2, ist[7], reserved3;
    uint16_t reserved4, iomap;
} __attribute__((packed));
static struct tss64 tss;
static uint8_t fault_stack[16384] __attribute__((aligned(16)));
extern void load_gdt64(const struct table_pointer *);
void gdt_install(void) {
    gdt[1] = UINT64_C(0x00af9a000000ffff);
    gdt[2] = UINT64_C(0x00cf92000000ffff);
    gdt[3] = UINT64_C(0x00affa000000ffff);
    gdt[4] = UINT64_C(0x00cff2000000ffff);
    uintptr_t base = (uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    gdt[5] = limit | ((base & 0xffffff) << 16) | (UINT64_C(0x89) << 40) |
             (((base >> 24) & 0xff) << 56);
    gdt[6] = base >> 32;
    tss.iomap = sizeof(tss);
    tss.ist[0] = (uintptr_t)(fault_stack + sizeof(fault_stack));
    struct table_pointer ptr = { sizeof(gdt)-1, (uintptr_t)gdt };
    load_gdt64(&ptr);
    __asm__ volatile("ltr %w0" :: "r"((uint16_t)0x28));
}
void tss_set_kernel_stack(uintptr_t top) { tss.rsp[0] = top; }
struct gate {
    uint16_t low, selector;
    uint8_t ist, flags;
    uint16_t middle;
    uint32_t high, reserved;
} __attribute__((packed));
static struct gate idt[256];
extern void *isr64_table[];
void idt_install(void) {
    for (unsigned i=0; i<256; ++i) {
        uintptr_t addr=(uintptr_t)isr64_table[i];
        idt[i]=(struct gate){addr,8,i==8 ? 1 : 0,i==128 ? 0xee : 0x8e,
                              addr>>16,addr>>32,0};
    }
    struct table_pointer ptr={sizeof(idt)-1,(uintptr_t)idt};
    __asm__ volatile("lidt %0" :: "m"(ptr));
}
extern void exception_handler(interrupt_frame_t *);
extern void interrupt_handler(interrupt_frame_t *);
extern void syscall_handler(interrupt_frame_t *);
void dispatch64(interrupt_frame_t *frame) {
    if (frame->int_num==128) syscall_handler(frame);
    else if (frame->int_num<32) exception_handler(frame);
    else if (frame->int_num<48) interrupt_handler(frame);
    __asm__ volatile("cli" ::: "memory");
    task_return_to_user(frame);
}
void _init(void) {}
/* GCC may lower aggregate copies to these even in freestanding code. */
void *memcpy(void *d,const void *s,size_t n) { return kmemcpy(d,s,n); }
void *memset(void *d,int c,size_t n) { return kmemset(d,c,n); }
void *memmove(void *d,const void *s,size_t n) { return kmemmove(d,s,n); }
